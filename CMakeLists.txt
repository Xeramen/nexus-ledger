cmake_minimum_required(VERSION 3.20)
project(nexus-ledger VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Поиск зависимостей
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)  # Добавил COMPONENTS system
find_package(nlohmann_json REQUIRED)
find_package(prometheus-cpp CONFIG REQUIRED)

# Все исходные файлы проекта
set(SOURCES
    src/main.cpp
    # Криптография
    src/crypto/crypto.cpp
    # Хранилище
    src/storage/ledger_db.cpp
    # Блокчейн
    src/blockchain/transaction.cpp
    src/blockchain/block.cpp
    src/blockchain/blockchain.cpp
    # Сеть
    src/network/peer.cpp
    src/network/server.cpp
    src/network/client.cpp
    # Ядро
    src/core/node.cpp
    # Метрики (будет позже)
    src/metrics/metrics_registry.cpp
)

# Создаём исполняемый файл
add_executable(${PROJECT_NAME} ${SOURCES})

# Подключаем директории с заголовочными файлами
target_include_directories(${PROJECT_NAME} PRIVATE
    ${OPENSSL_INCLUDE_DIR}
    ${SQLITE3_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Линкуем библиотеки
target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenSSL::Crypto
    SQLite::SQLite3
    ${Boost_LIBRARIES}
    nlohmann_json::nlohmann_json
    pthread
    prometheus-cpp::core
    prometheus-cpp::pull
)

# Копируем schema.sql в директорию сборки
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/schema.sql 
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Опции компиляции
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

# Для отладки - показываем найденные библиотеки
message(STATUS "Project configured successfully")
message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
message(STATUS "SQLite3 version: ${SQLite3_VERSION}")
message(STATUS "Boost version: ${Boost_VERSION}")