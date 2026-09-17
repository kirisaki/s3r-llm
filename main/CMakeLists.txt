idf_component_register(SRCS "main.cpp" "display.cpp" "chat.cpp" "serial.cpp" "llm.cpp" "i2c_bus.cpp" "imu.cpp" "button.cpp" "settings.cpp" "wifi.cpp" "api.cpp"
                    INCLUDE_DIRS ".")

# `idf.py tokenizer-flash model-flash` writes the model; it is not part of
# `idf.py flash` because it is big and rarely changes.
set(models_dir "${PROJECT_DIR}/models")
if(EXISTS "${models_dir}/model_neo_q4.bin" AND EXISTS "${models_dir}/tok_neo.bin")
    esp_partition_register_target(tokenizer "${models_dir}/tok_neo.bin")
    esp_partition_register_target(model "${models_dir}/model_neo_q4.bin")
else()
    message(WARNING "No model in ${models_dir}, run tools/fetch_model.sh to get one")
endif()
