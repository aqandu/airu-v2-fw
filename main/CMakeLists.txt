set(COMPONENT_SRCS "app_main.c")
set(COMPONENT_ADD_INCLUDEDIRS ".")

# Embed the server root certificate into the final binary
set(COMPONENT_EMBED_TXTFILES ${PROJECT_PATH}/server_certs/ca_cert.pem)

# Embed the MQTT server root certificate into the final binary
set(COMPONENT_EMBED_TXTFILES ${PROJECT_PATH}/server_certs/ca.pem)

register_component()
