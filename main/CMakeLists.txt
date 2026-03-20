set(INCLUDES "." "./ble" "./onenet" "./wifi_ap" "./lvgl" "./bsp" "./hardware/rc522" "./hardware/as608" "./hardware/servo" 
            "./ui/generated" "./ui/custom" "./ui/generated/guider_customer_fonts")
file(GLOB_RECURSE SOURCES ./*.c)

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS ${INCLUDES}
                    )
