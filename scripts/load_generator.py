#!/usr/bin/env python3
# scripts/load_generator.py

import requests
import random
import time
import sys
import threading
import json
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor

# ============================================================
# КОНФИГУРАЦИЯ
# ============================================================

# HTTP API порты нод (P2P порт + 1000)
NODES = [
    {"name": "node1", "p2p": 8000, "http": 9000, "metrics": 9100},
    {"name": "node2", "p2p": 8001, "http": 9001, "metrics": 9101},
    {"name": "node3", "p2p": 8002, "http": 9002, "metrics": 9102},
    #{"name": "node4", "p2p": 8003, "http": 9003, "metrics": 9103},
    #{"name": "node5", "p2p": 8004, "http": 9004, "metrics": 9104},
    #{"name": "node6", "p2p": 8005, "http": 9005, "metrics": 9105},
]

# Генерация адресов
# Список получателей (может быть любым)
RECIPIENTS = [f"user_{i:03d}" for i in range(1, 101)] + [f"miner_{i:03d}" for i in range(1, 21)]
# Отправитель только один – genesis_miner (у него есть деньги)
SENDER = "genesis_miner"

# Статистика
stats = {
    "total": 0,
    "success": 0,
    "failed": 0,
    "start_time": time.time(),
    "errors": {}
}

stats_lock = threading.Lock()

# ============================================================
# ФУНКЦИИ
# ============================================================

def print_banner():
    print("""
╔═══════════════════════════════════════════════════════════════════════════════╗
║                     NEXUS LEDGER LOAD GENERATOR                               ║
║            Генератор нагрузки для тестирования P2P сети                       ║
╚═══════════════════════════════════════════════════════════════════════════════╝
    """)

def get_network_stats():
    """Получает текущие метрики сети для отображения"""
    result = {}
    for node in NODES:
        try:
            resp = requests.get(f"http://localhost:{node['metrics']}/metrics", timeout=2)
            if resp.status_code == 200:
                lines = resp.text.split('\n')
                for line in lines:
                    if 'nexus_blockchain_height' in line and not line.startswith('#'):
                        result[f"{node['name']}_height"] = line.split()[-1]
                    if 'nexus_peers_active' in line and not line.startswith('#'):
                        result[f"{node['name']}_peers"] = line.split()[-1]
                    if 'nexus_mempool_size' in line and not line.startswith('#'):
                        result[f"{node['name']}_mempool"] = line.split()[-1]
        except:
            pass
    return result

def send_transaction(node, from_addr, to_addr, amount, retry=2):
    """Отправляет одну транзакцию через HTTP API с повторами"""
    url = f"http://localhost:{node['http']}/transaction"
    tx_data = {
        "from": from_addr,
        "to": to_addr,
        "amount": round(amount, 2),
        "fee": 0.001,
        "nonce": 0          # nonce = 0, проверка в ноде отключена
    }
    
    for attempt in range(retry):
        try:
            response = requests.post(url, json=tx_data, timeout=5)
            if response.status_code == 200 and "OK" in response.text:
                return True, None
            else:
                # Если ошибка не связана с соединением, не повторяем
                if response.status_code >= 500:
                    continue
                return False, f"HTTP {response.status_code}"
        except requests.exceptions.ConnectionError:
            if attempt < retry-1:
                time.sleep(0.5)
                continue
            return False, "Connection refused"
        except requests.exceptions.Timeout:
            if attempt < retry-1:
                time.sleep(1)
                continue
            return False, "Timeout"
        except Exception as e:
            return False, str(e)[:50]
    return False, "Max retries exceeded"

def worker(worker_id, duration, rate_per_second):
    """Рабочий поток"""
    end_time = time.time() + duration if duration > 0 else float('inf')
    
    interval_ms = 1000.0 / rate_per_second if rate_per_second > 0 else 0
    
    while time.time() < end_time:
        node = random.choice(NODES)
        from_addr = SENDER
        to_addr = random.choice(RECIPIENTS)
        while from_addr == to_addr:
            to_addr = random.choice(RECIPIENTS)
        amount = random.uniform(0.1, 100)
        
        success, error = send_transaction(node, from_addr, to_addr, amount)
        
        with stats_lock:
            stats["total"] += 1
            if success:
                stats["success"] += 1
            else:
                stats["failed"] += 1
                if error not in stats["errors"]:
                    stats["errors"][error] = 0
                stats["errors"][error] += 1
        
        if interval_ms > 0:
            time.sleep(interval_ms / 1000.0)
        else:
            time.sleep(random.uniform(0.1, 0.5))

def print_stats(stop_event):
    """Поток для вывода статистики"""
    last_total = 0
    last_time = time.time()
    
    while not stop_event.is_set():
        time.sleep(5)
        
        with stats_lock:
            current_total = stats["total"]
            current_success = stats["success"]
            current_failed = stats["failed"]
        
        elapsed = time.time() - stats["start_time"]
        tps = current_total / elapsed if elapsed > 0 else 0
        
        now = time.time()
        instant_tps = (current_total - last_total) / (now - last_time) if (now - last_time) > 0 else 0
        last_total = current_total
        last_time = now
        
        success_rate = (current_success / current_total * 100) if current_total > 0 else 0
        
        network_stats = get_network_stats()
        
        print(f"\n{'='*80}")
        print(f"📊 [{datetime.now().strftime('%H:%M:%S')}] СТАТИСТИКА")
        print(f"{'='*80}")
        print(f"   📦 Всего транзакций: {current_total}")
        print(f"   ✅ Успешно: {current_success}")
        print(f"   ❌ Ошибок: {current_failed}")
        print(f"   📈 Средний TPS: {tps:.2f} | Мгновенный TPS: {instant_tps:.2f}")
        print(f"   🎯 Успешность: {success_rate:.1f}%")
        print(f"   ⏱️  Время работы: {elapsed:.0f} сек")
        print()
        if network_stats:
            print(f"   🌐 СОСТОЯНИЕ СЕТИ:")
            for name, value in network_stats.items():
                if "height" in name:
                    print(f"      {name}: {value}")
        print(f"{'='*80}")

def main():
    print_banner()
    
    print("⚙️  НАСТРОЙКА ГЕНЕРАТОРА")
    print("-" * 40)
    print(f"   Доступные ноды: {len(NODES)}")
    print()
    
    try:
        duration_input = input("⏱️  Время работы (секунд, Enter для бесконечно): ").strip()
        duration = int(duration_input) if duration_input else 0
    except:
        duration = 0
    
    try:
        rate_input = input("📊 Транзакций в секунду (Enter для 10): ").strip()
        rate = int(rate_input) if rate_input else 10
    except:
        rate = 10
    
    try:
        threads_input = input("🔧 Количество потоков (Enter для 4): ").strip()
        num_workers = int(threads_input) if threads_input else 4
    except:
        num_workers = 4
    
    num_workers = max(1, min(20, num_workers))
    rate_per_worker = rate / num_workers if rate > 0 else 0
    
    print()
    print("─" * 80)
    print(f"🚀 ЗАПУСК ГЕНЕРАТОРА")
    print(f"   • Потоков: {num_workers}")
    print(f"   • Целевой TPS: {rate}")
    print(f"   • Транзакций на поток: {rate_per_worker:.2f}/сек")
    if duration > 0:
        print(f"   • Время работы: {duration} сек")
    else:
        print(f"   • Время работы: бесконечно (Ctrl+C для остановки)")
    print("─" * 80)
    
    stop_event = threading.Event()
    
    stats_thread = threading.Thread(target=print_stats, args=(stop_event,))
    stats_thread.daemon = True
    stats_thread.start()
    
    with ThreadPoolExecutor(max_workers=num_workers) as executor:
        futures = []
        for i in range(num_workers):
            future = executor.submit(worker, i, duration, rate_per_worker)
            futures.append(future)
        
        try:
            if duration > 0:
                time.sleep(duration)
                print("\n⏰ Время работы истекло. Остановка...")
            else:
                while True:
                    time.sleep(1)
        except KeyboardInterrupt:
            print("\n\n🛑 Остановка генератора...")
        
        stop_event.set()
        
        for future in futures:
            try:
                future.result(timeout=2)
            except:
                pass
    
    elapsed = time.time() - stats["start_time"]
    print("\n" + "═" * 80)
    print("📊 ИТОГОВАЯ СТАТИСТИКА")
    print("═" * 80)
    print(f"⏱️  Время работы: {elapsed:.1f} сек")
    print(f"📦 Всего транзакций: {stats['total']}")
    print(f"✅ Успешно: {stats['success']}")
    print(f"❌ Ошибок: {stats['failed']}")
    if stats['total'] > 0:
        print(f"📈 Средний TPS: {stats['total']/elapsed:.2f}")
        print(f"🎯 Успешность: {stats['success']/stats['total']*100:.1f}%")
    
    if stats['errors']:
        print(f"\n❌ ТИПЫ ОШИБОК:")
        for error, count in sorted(stats['errors'].items(), key=lambda x: -x[1]):
            print(f"   • {error}: {count} раз")
    
    print("═" * 80)

if __name__ == "__main__":
    main()