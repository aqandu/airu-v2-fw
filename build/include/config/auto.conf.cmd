deps_config := \
	/Users/tombo/esp/esp-idf/components/app_trace/Kconfig \
	/Users/tombo/esp/esp-idf/components/aws_iot/Kconfig \
	/Users/tombo/esp/esp-idf/components/bt/Kconfig \
	/Users/tombo/esp/esp-idf/components/driver/Kconfig \
	/Users/tombo/esp/esp-idf/components/esp32/Kconfig \
	/Users/tombo/esp/esp-idf/components/esp_adc_cal/Kconfig \
	/Users/tombo/esp/esp-idf/components/esp_event/Kconfig \
	/Users/tombo/esp/esp-idf/components/esp_http_client/Kconfig \
	/Users/tombo/esp/esp-idf/components/esp_http_server/Kconfig \
	/Users/tombo/esp/esp-idf/components/ethernet/Kconfig \
	/Users/tombo/esp/esp-idf/components/fatfs/Kconfig \
	/Users/tombo/esp/esp-idf/components/freemodbus/Kconfig \
	/Users/tombo/esp/esp-idf/components/freertos/Kconfig \
	/Users/tombo/esp/esp-idf/components/heap/Kconfig \
	/Users/tombo/esp/esp-idf/components/libsodium/Kconfig \
	/Users/tombo/esp/esp-idf/components/log/Kconfig \
	/Users/tombo/esp/esp-idf/components/lwip/Kconfig \
	/Users/tombo/esp/esp-idf/components/mbedtls/Kconfig \
	/Users/tombo/esp/esp-idf/components/mdns/Kconfig \
	/Users/tombo/esp/esp-idf/components/mqtt/Kconfig \
	/Users/tombo/esp/esp-idf/components/nvs_flash/Kconfig \
	/Users/tombo/esp/esp-idf/components/openssl/Kconfig \
	/Users/tombo/esp/esp-idf/components/pthread/Kconfig \
	/Users/tombo/esp/esp-idf/components/spi_flash/Kconfig \
	/Users/tombo/esp/esp-idf/components/spiffs/Kconfig \
	/Users/tombo/esp/esp-idf/components/tcpip_adapter/Kconfig \
	/Users/tombo/esp/esp-idf/components/unity/Kconfig \
	/Users/tombo/esp/esp-idf/components/vfs/Kconfig \
	/Users/tombo/esp/esp-idf/components/wear_levelling/Kconfig \
	/Users/tombo/esp/esp-idf/components/app_update/Kconfig.projbuild \
	/Users/tombo/esp/esp-idf/components/bootloader/Kconfig.projbuild \
	/Users/tombo/esp/esp-idf/components/esptool_py/Kconfig.projbuild \
	/Users/tombo/esp/airu-v2-fw/main/Kconfig.projbuild \
	/Users/tombo/esp/esp-idf/components/partition_table/Kconfig.projbuild \
	/Users/tombo/esp/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)

ifneq "$(IDF_TARGET)" "esp32"
include/config/auto.conf: FORCE
endif
ifneq "$(IDF_CMAKE)" "n"
include/config/auto.conf: FORCE
endif

$(deps_config): ;
