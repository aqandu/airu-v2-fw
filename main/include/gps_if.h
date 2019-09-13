/*
 * gps_if.h
 *
 *  Created on: Jan 3, 2019
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_GPS_IF_H_
#define MAIN_INCLUDE_GPS_IF_H_


/**************************************************************************/
/**
 Different commands to set the update rate from once a second (1 Hz) to 10 times a second (10Hz)
 Note that these only control the rate at which the position is echoed, to actually speed up the
 position fix you must also send one of the position fix rate commands below too. */
#define PMTK_SET_NMEA_UPDATE_100_MILLIHERTZ  "$PMTK220,10000*2F\r\n"  ///< Once every 10 seconds, 100 millihertz.
#define PMTK_SET_NMEA_UPDATE_200_MILLIHERTZ  "$PMTK220,5000*1B\r\n"   ///< Once every 5 seconds, 200 millihertz.
#define PMTK_SET_NMEA_UPDATE_1HZ  "$PMTK220,1000*1F\r\n"              ///<  1 Hz
#define PMTK_SET_NMEA_UPDATE_2HZ  "$PMTK220,500*2B\r\n"               ///<  2 Hz
#define PMTK_SET_NMEA_UPDATE_5HZ  "$PMTK220,200*2C\r\n"               ///<  5 Hz
#define PMTK_SET_NMEA_UPDATE_10HZ "$PMTK220,100*2F\r\n"               ///< 10 Hz
// Position fix update rate commands.
#define PMTK_API_SET_FIX_CTL_100_MILLIHERTZ  "$PMTK300,10000,0,0,0,0*2C\r\n"  ///< Once every 10 seconds, 100 millihertz.
#define PMTK_API_SET_FIX_CTL_200_MILLIHERTZ  "$PMTK300,5000,0,0,0,0*18\r\n"   ///< Once every 5 seconds, 200 millihertz.
#define PMTK_API_SET_FIX_CTL_1HZ  "$PMTK300,1000,0,0,0,0*1C\r\n"              ///< 1 Hz
#define PMTK_API_SET_FIX_CTL_5HZ  "$PMTK300,200,0,0,0,0*2F\r\n"               ///< 5 Hz
// Can't fix position faster than 5 times a second!

#define PMTK_SET_BAUD_57600 "$PMTK251,57600*2C\r\n" ///< 57600 bps
#define PMTK_SET_BAUD_9600 "$PMTK251,9600*17\r\n"   ///<  9600 bps

#define PMTK_SET_NMEA_OUTPUT_RMCONLY "$PMTK314,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29\r\n"  ///< turn on only the second sentence (GPRMC)
#define PMTK_SET_NMEA_OUTPUT_RMCGGA "$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n"   ///< turn on GPRMC and GGA
#define PMTK_SET_NMEA_OUTPUT_ALLDATA "$PMTK314,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n"  ///< turn on ALL THE DATA
#define PMTK_SET_NMEA_OUTPUT_OFF "$PMTK314,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n"      ///< turn off output

// to generate your own sentences, check out the MTK command datasheet and use a checksum calculator
// such as the awesome http://www.hhhh.org/wiml/proj/nmeaxor.html

#define PMTK_LOCUS_STARTLOG  "$PMTK185,0*22\r\n"          ///< Start logging data
#define PMTK_LOCUS_STOPLOG "$PMTK185,1*23\r\n"            ///< Stop logging data
#define PMTK_LOCUS_STARTSTOPACK "$PMTK001,185,3*3C\r\n"   ///< Acknowledge the start or stop command
#define PMTK_LOCUS_QUERY_STATUS "$PMTK183*38\r\n"         ///< Query the logging status
#define PMTK_LOCUS_ERASE_FLASH "$PMTK184,1*22\r\n"        ///< Erase the log flash data
#define LOCUS_OVERLAP 0                               ///< If flash is full, log will overwrite old data with new logs
#define LOCUS_FULLSTOP 1                              ///< If flash is full, logging will stop

#define PMTK_ENABLE_SBAS "$PMTK313,1*2E\r\n"              ///< Enable search for SBAS satellite (only works with 1Hz output rate)
#define PMTK_ENABLE_WAAS "$PMTK301,2*2E\r\n"              ///< Use WAAS for DGPS correction data

#define PMTK_PERIODIC "$PMTK225,2,3000,12000,18000,72000*15\r\n" ///< On for 3 sec, off for 12, if need be on for 18, off for 72>
#define PMTK_STANDBY "$PMTK161,0*28\r\n"              	  ///< standby command & boot successful message
#define PMTK_STANDBY_SUCCESS "$PMTK001,161,3*36\r\n"  	  ///< Not needed currently
#define PMTK_AWAKE "$PMTK010,002*2D\r\n"              	  ///< Wake up

#define PMTK_Q_RELEASE "$PMTK605*31\r\n"              	  ///< ask for the release and version


#define PGCMD_ANTENNA "$PGCMD,33,1*6C\r\n"            	  ///< request for updates on antenna status
#define PGCMD_NOANTENNA "$PGCMD,33,0*6D\r\n"          	  ///< don't show antenna status messages

/**************************************************************************/

typedef struct {
	float lat;
	float lon;
	float alt;
	struct tm timeinfo;
} esp_gps_t;

void GPS_Tx(const char* pmtk);
esp_err_t GPS_Initialize(void);
void GPS_Poll(esp_gps_t* gps);
void GPS_SetSystemTimeFromGPS(void);


#endif /* MAIN_INCLUDE_GPS_IF_H_ */
