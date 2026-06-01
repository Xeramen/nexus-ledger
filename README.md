# 🔗 Nexus Ledger - Децентрализованная P2P сеть с распределённым реестром

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Boost](https://img.shields.io/badge/Boost-1.74-orange.svg)](https://www.boost.org/)
[![Prometheus](https://img.shields.io/badge/Prometheus-2.53-red.svg)](https://prometheus.io/)
[![Grafana](https://img.shields.io/badge/Grafana-10.4-yellow.svg)](https://grafana.com/)

## 📖 О проекте

**Nexus Ledger** — это децентрализованная P2P сеть с распределённым реестром (блокчейном), реализованная на C++20. Проект демонстрирует работу полностью децентрализованной сети, где каждый узел независим, но синхронизирован с другими через P2P протокол.

### 🎯 Ключевые особенности

- ✅ **Полностью децентрализованная P2P сеть** (нет единой точки отказа)
- ✅ **Блокчейн с Proof-of-Work** (майнинг новых блоков)
- ✅ **Автоматическая синхронизация блокчейна** между узлами
- ✅ **Gossip протокол** для обнаружения пиров
- ✅ **HTTP API** для отправки транзакций
- ✅ **Prometheus метрики** для мониторинга
- ✅ **Grafana дашборды** для визуализации
- ✅ **SQLite хранилище** для каждого узла
- ✅ **Поддержка 10+ узлов** в одной сети

### Автоматическая установка

```bash
git clone https://github.com/yourusername/nexus-ledger.git
cd nexus-ledger
chmod +x scripts/install.sh
sudo ./scripts/install.sh

```

### Ручная установка

```bash
# 1. Установка зависимостей
sudo apt install -y build-essential cmake libboost-all-dev \
    libssl-dev libsqlite3-dev nlohmann-json3-dev prometheus-cpp-dev

# 2. Сборка
mkdir build && cd build
cmake .. && make -j4

# 3. Создание базы данных
sqlite3 ../data/node1.db < ../src/storage/schema.sql

# 4. Вывод возможных команд
./nexus-ledger
```