#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_vfs_fat.h"
#include "sd_if.h"
#include "tcpip_adapter.h"
//#include "esp_tls.h"

// http_client example
#include "esp_http_client.h"

// http_request example
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

// local
#include "http_file_upload.h"

#define SERVER_FILENAME_LEN	64				/* Max filename size on server (including path) */
#define CHUNK_SZ_STR_LEN	16				/* The chunk size HEX string to send over HTTP: (8 HEX) + "\r\n" + '\0' = 11 (16 to be clean)*/

#define HOSTNAME 	"192.168.1.169"
#define PORT		"80"
#define ROUTE		"/"
#define WEB_URL 	"http://" HOSTNAME ":" PORT ROUTE
#define BOUNDARY 	"-----z9Y6ivbmznZNE23n-----"		/* Don't see a reason to make this on the fly */

static uint32_t CHUNK_SZ = 4096; 				/* Max size of chunk (Max is 2^32)*/
static uint32_t CHUNK_DATA_SZ = 0;				/* File data starts at offset CHUNK_SZ_STR_LEN in buffer */

static const char* TAG = "HTTP";
static const char* TAG_INIT = "HTTP-INIT";
static const char* TAG_HEAP = "HEAP";

static const char* newline = "--------------------------------------------------";

static const char* MULTIPART_REQUEST = \
		"POST " WEB_URL " HTTP/1.0\r\n"
		"Host:" HOSTNAME "\r\n"
		"User-Agent: esp-idf/3.0 esp32\r\n"
		"Content-Type:multipart/form-data; boundary=" BOUNDARY "\r\n"
		"Transfer-Encoding:chunked\r\n"
		"\r\n";

static const char *MULTIPART_BODY_HEADER_TEMPLATE = \
		"--" BOUNDARY "\r\n"
		"Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
		"Content-Type: text/csv\r\n"
		"\r\n";

typedef enum {
	STATE_HEADER = 0,	/* (0) HEADER 		*/
	STATE_BODY,			/* (1) BODY (FILE) 	*/
	STATE_BOUNDARY,		/* (2) BOUNDARY 	*/
	STATE_TERMINATOR	/* (3) Terminator 	*/
} http_post_states_t;

typedef struct http_poster{
	char* hostname;		/* Server hostname (air.eng.utah.edu) */
	char* port;			/* Server port ("80") */
	char* fn_base;		/* SD card filename (basename) "1993-08-05.csv" */
	char fn_src[SD_FILENAME_LENGTH];		/* Full source path "/sdcard/1993-08-05.csv" */
	char fn_dst[SERVER_FILENAME_LEN];		/* Full destiation path "<MAC>_1993-08-05.csv" */
	FILE* fp;			/* File pointer to SD card file */
	struct stat st;		/* Stats on SD card file */
	int sock;			/* Socket to write data over */
	char *sz_buf;		/* Buffer to put chunk size HEX string */
	char *tx_buf;		/* Buffer to put data in */
	uint32_t slen;		/* String length of value in tx_buf - Use to stop calling strlen()*/
}http_poster_t;

char *tx_buf;

/* Static function declarations */
static int http_init(http_poster_t* poster);
static int http_connect(http_poster_t* poster);
static int http_write_request(http_poster_t* poster);
static int http_write_body(http_poster_t* poster);
static int _http_write_body_closer(http_poster_t* poster);
static int http_write_body_headers_chunked(http_poster_t* poster);
static int http_write_file_chunked(http_poster_t* poster);
static int http_write_chunk(http_poster_t* poster);
static int _http_write_chunk_size(http_poster_t* poster);
static int _http_write_chunk_data(http_poster_t* poster);
static int read_http_response(http_poster_t* poster);
static void http_post_cleanup(http_poster_t* poster);
/* ------------------------------------------------------------------------ */

void set_chunk_size(uint32_t chunk_size)
{
	/*
	 * Chunk size must cover all body headers! Otherwise filename won't get correctly inserted.
	 * Can fix this several ways:
	 * 	1. another char[] of max length. Find and insert the desired filename, then shift all
	 *		data after end of filename back to end of filename.
	 *	2. break up MULTIPART_BODY_HEADER_TEMPLATE into two templates: before and after filename.
	 *		Send 'before' chunks, send filename chunks, send 'after' chunks.
	 *	3. While sending chunks, if we come to the "%s" end the chunk there. Then filename chunks,
	 *		then back to TEMPLATE chunks.
	 *
	 * I don't want to do any of these, so just make sure you have like 250 bytes of heap, ok?
	 */
	uint32_t heap = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
	ESP_LOGI(TAG_HEAP, "Available heap: %d", heap);
//	uint32_t heap = esp_get_free_heap_size() - 1024;
	int min_size = strlen(MULTIPART_BODY_HEADER_TEMPLATE) \
					+ SERVER_FILENAME_LEN \
					- 2 /* "%s" */ \
					- 2 /* CRLF */ \
					- 1 /* '\0' -- Needed? Can't remember. Too tired. */;

	/* Use the maximum */
	if(chunk_size == 0){
		ESP_LOGI(TAG_HEAP, "Using max heap size: %d", heap);
		chunk_size = heap;
	}

	/* Requested chunk was too big. Get it down */
	if(chunk_size > heap){
		ESP_LOGE(TAG_HEAP, "Chunk size larger than heap: (%d). Upgrade to heap size", heap);
		chunk_size = heap;
	}

	/* Check minimum heap available */
	if(min_size > heap){
		ESP_LOGE(TAG_HEAP, "Not enough heap. Available heap: %d. Minimum size needed: %d", heap, min_size);
		return;
	}

	/* Do we need to increase chunk size to minimum size? */
	if(chunk_size < min_size){
		ESP_LOGE(TAG_HEAP, "Chunk size too small. Upgrading to absolute minimum: %d", min_size);
		chunk_size = min_size;
	}

	ESP_LOGI(TAG_HEAP, "Using %d byte chunks from heap.", chunk_size);
	CHUNK_SZ = chunk_size;
	CHUNK_DATA_SZ = CHUNK_SZ - 3;
}

static int http_init(http_poster_t* poster)
{
	ESP_LOGI(TAG, "\n\r%s\n\rINITIALIZATION\n\r%s", newline, newline);

	set_chunk_size(CHUNK_SZ);

	poster->fp = NULL;
	poster->sock = 0;

	// Set the source path
	if(snprintf(poster->fn_src, SD_FILENAME_LENGTH, "/sdcard/%s", poster->fn_base) > SD_FILENAME_LENGTH){
		ESP_LOGE(TAG, "Source path/filename too long: %s", poster->fn_src);
		return ESP_FAIL;
	}

	// Set the destination file TODO: set MAC Address
	if(snprintf(poster->fn_dst, SERVER_FILENAME_LEN, "%s_%s", (const char*)"AABBCCDDEEFF", poster->fn_base) > SERVER_FILENAME_LEN){
		ESP_LOGE(TAG, "Destination filename too long: %s", poster->fn_dst);
	}

	// Get the file statistics
	if(stat(poster->fn_src, &poster->st) != ESP_OK){
		ESP_LOGE(TAG, "No file stats for %s", poster->fn_src);
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Filename:  %s", poster->fn_base);
	ESP_LOGI(TAG, "File Path: %s", poster->fn_src);
	ESP_LOGI(TAG, "File Size: %li", poster->st.st_size);

	if(poster->st.st_size == 0){
		ESP_LOGE(TAG, "File is 0 bytes: %s", poster->fn_src);
		return ESP_FAIL;
	}

	// Set the buffers
	if((poster->tx_buf = malloc(CHUNK_SZ)) == NULL){
		ESP_LOGE(TAG_INIT, "not enough heap for tx_buf");
		set_chunk_size(0);
		if((poster->tx_buf = malloc(CHUNK_SZ)) == NULL){
			ESP_LOGE(TAG_INIT, "Still not enough! Requested %d but only had %d", CHUNK_SZ, esp_get_free_heap_size());
			return ESP_FAIL;
		}
	}
	if((poster->sz_buf = malloc(CHUNK_SZ_STR_LEN)) == NULL){
		ESP_LOGE(TAG, "not enough heap for sz_buf");
		return ESP_FAIL;
	}
	poster->slen = 0;

	ESP_LOGI(TAG, "\n\r%s", newline);
	return ESP_OK;

}

static int http_connect(http_poster_t* poster)
{
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *res;
	struct in_addr *addr;
	int s;

		esp_err_t err = getaddrinfo(poster->hostname, poster->port, &hints, &res);

		if(err != ESP_OK || res == NULL){
			ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
			return ESP_FAIL;
		}

		addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
		ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

		s = socket(res->ai_family, res->ai_socktype, 0);
		if(s < 0){
			ESP_LOGE(TAG, "... Failed to allocate socket.");
			freeaddrinfo(res);
			return ESP_FAIL;
		}

		ESP_LOGI(TAG, "... Allocated socket");

		if(connect(s, res->ai_addr, res->ai_addrlen) != 0){
			ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
			close(s);
			freeaddrinfo(res);
			return ESP_FAIL;
		}

		ESP_LOGI(TAG, "... connected");
		freeaddrinfo(res);

		poster->sock = s;
		return ESP_OK;
}

static int http_write_request(http_poster_t* poster)
{
	int wlen;

	ESP_LOGI(TAG, "REQUEST:\r\n%s%s", MULTIPART_REQUEST, newline);

	if ((wlen = write(poster->sock, MULTIPART_REQUEST, strlen(MULTIPART_REQUEST))) != strlen(MULTIPART_REQUEST)){
		ESP_LOGE(TAG, "Request failed. errno: %d", wlen);
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "Request success");

	return ESP_OK;
}

static int _http_write_body_closer(http_poster_t* poster)
{
	/* Write the boundary */
	ESP_LOGI(TAG, "Writing closing boundary");
	poster->slen = snprintf(poster->tx_buf, CHUNK_DATA_SZ, "--%s--\n\r", BOUNDARY);
	if(http_write_chunk(poster)){
		return ESP_FAIL;
	}

	/* Write the terminator */
	ESP_LOGI(TAG, "Writing terminator");
	poster->tx_buf[0] = '\0';
	poster->slen = strlen(poster->tx_buf);
	if(http_write_chunk(poster)){
		return ESP_FAIL;
	}
	return ESP_OK;
}

static int http_write_body(http_poster_t* poster)
{

	/* Write the param headers */
	ESP_LOGI(TAG, "Writing body headers...");
	poster->slen = snprintf(poster->tx_buf, CHUNK_DATA_SZ, MULTIPART_BODY_HEADER_TEMPLATE, poster->fn_dst);
	printf("%s\n\r", poster->tx_buf);
	if(http_write_chunk(poster)){
		return ESP_FAIL;
	}

	/* Write the file */
	ESP_LOGI(TAG, "Writing file...");
	if(http_write_file_chunked(poster) != ESP_OK){
		return ESP_FAIL;
	}

	/* Write boundary and terminator */
	if(_http_write_body_closer(poster) != ESP_OK){
		return ESP_FAIL;
	}

	return ESP_OK;
}

static int http_write_file_chunked(http_poster_t* poster)
{
	int64_t flen = poster->st.st_size;
	uint32_t packets_sent = 0;

	if(flen <= 0){
		ESP_LOGE(TAG, "Bad file");
		http_post_cleanup(poster);
		return ESP_FAIL;
	}

	if((poster->fp = sd_fopen(poster->fn_base)) == NULL){
		return ESP_FAIL;
	}

	// Read and write SD card file
	do{
		// read file chunk and write
		poster->slen = fread(poster->tx_buf, 1, CHUNK_DATA_SZ, poster->fp);
		poster->tx_buf[poster->slen] = '\0'; // Terminate the fread chunk

//		ESP_LOGI(TAG, "SD Chunk (Size: %d):\n\r%s\n\r%s", poster->slen, poster->tx_buf, newline);

		// flen starts at file size, then decreases by amount of file read
		flen -= poster->slen;

		// Write the chunk to the socket
		if(http_write_chunk(poster) != ESP_OK){
			return ESP_FAIL;
		}

		packets_sent++;
		ESP_LOGI(TAG, "Packets sent: %d", packets_sent);

	} while(flen > 0);

	return ESP_OK;

}

static int http_write_chunk(http_poster_t* poster)
{
	esp_err_t err;

	if((err = _http_write_chunk_size(poster)) != ESP_OK){
		return err;
	}

	return _http_write_chunk_data(poster);
}

static int _http_write_chunk_size(http_poster_t* poster)
{
	int32_t r;
	int wlen = snprintf(poster->sz_buf, CHUNK_SZ_STR_LEN, "%x\r\n", poster->slen);

//	ESP_LOGI(TAG, "Chunk Size Packet (Size: %d / 0x%x ):\r\n%s%s", poster->slen, poster->slen, poster->sz_buf, newline);

	if((r = write(poster->sock, poster->sz_buf, wlen)) != wlen){
		ESP_LOGE(TAG, "%s Write error (Size: %d / %d)", __func__, r, wlen);
		return ESP_FAIL;
	}
	return ESP_OK;
}

static int _http_write_chunk_data(http_poster_t* poster)
{
	int32_t r;
	poster->slen += sprintf(poster->tx_buf + poster->slen, "\r\n");

//	ESP_LOGI(TAG, "Chunk Packet:\n\r%s%s", poster->tx_buf, newline);

	if((r = write(poster->sock, poster->tx_buf, poster->slen)) < 0){
		ESP_LOGE(TAG, "%s Write error (Size: %d / %d)", __func__, r, poster->slen);
		return ESP_FAIL;
	}
	poster->slen = 0;
	return ESP_OK;
}

static int read_http_response(http_poster_t* poster)
{
	int64_t r;
	char* ptr;
	char resp[4] = { 0 };
	int rcode;

	struct timeval receiving_timeout;
    receiving_timeout.tv_sec = 5;
    receiving_timeout.tv_usec = 0;
    if (setsockopt(poster->sock, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
            sizeof(receiving_timeout)) < 0) {
        ESP_LOGE(TAG, "... failed to set socket receiving timeout");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "... set socket receiving timeout success");

    // read out HTTP/1.0
	read(poster->sock, poster->tx_buf, 9);

	// read out response code
	read(poster->sock, resp, 3);
	rcode = atoi(resp);

	do{
		// read out the rest so we can close the communication
		r = read(poster->sock, poster->tx_buf, CHUNK_SZ);
	} while(r > 0);

	ESP_LOGI(TAG, "RCODE: %d", rcode);
    return rcode;
}

static void http_post_cleanup(http_poster_t* poster)
{
	ESP_LOGI(TAG, "Cleanup...");
	fclose(poster->fp);
	close(poster->sock);
	free(poster->tx_buf);
	free(poster->sz_buf);
}

esp_err_t http_upload_file_from_sd(const char* filename)
{
	http_poster_t p = {
			.hostname = HOSTNAME,
			.port = PORT,
			.fn_base = filename,
	};
	http_poster_t* poster = &p;

	if(http_init(poster) != ESP_OK){
		http_post_cleanup(poster);
		return ESP_FAIL;
	}

	/* Connect to server */
	if(http_connect(poster) != ESP_OK){
		http_post_cleanup(poster);
		return ESP_FAIL;
	}

	/* Send HTTP POST Request */
	if(http_write_request(poster) != ESP_OK){
		http_post_cleanup(poster);
		return ESP_FAIL;
	}

	printf("Send HTTP POST multipart/form-data body...\n\n\r");
	/* Send HTTP POST Body (file) */
	if(http_write_body(poster) != ESP_OK){
		http_post_cleanup(poster);
		return ESP_FAIL;
	}

	/* Read HTTP Server Response */
	if(read_http_response(poster) != ESP_OK){
		http_post_cleanup(poster);
		return ESP_FAIL;
	}

	return ESP_OK;
}
