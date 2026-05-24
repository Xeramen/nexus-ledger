#!/usr/bin/env python3
# scripts/load_generator.py

import requests
import random
import time
import sys
import threading
from datetime import datetime

# Конфигурация
NODES = [
    "http://localhost:8000",  # Нода 0 (P2P порт, не HTTP!)
    "http://localhost:8001",
    "http://localhost:8002",
    "http://localhost:8003",
    "http://localhost:8004",
    "http://localhost:8005",
]

ADDRESSES = [f"user_{i}" for i in range(1, 21)] + [f"miner_{i}" for i in range(1, 6)]
STATS = {"total": 0, "success": 0, "failed": 0, "start_time": time.time()}

def print_banner():
    print("""
╔══════════════════════════════════════════════════════════════╗
║                                                              ║
║     ███╗   ██╗███████╗██╗  ██╗██╗   ██╗███████╗             ║
║     ████╗  ██║██╔════╝╚██╗██╔╝██║   ██║██╔════╝             ║
║     ██╔██╗ ██║█████╗   ╚███╔╝ ██║   ██║███████╗             ║
║     ██║╚██╗██║██╔══╝   ██╔██╗ ██║   ██║╚════██║             ║
║     ██║ ╚████║███████╗██╔╝ ██╗╚██████╔╝███████║             ║
║     ╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝             ║
║                                                              ║
║              NEXUS LEDGER LOAD GENERATOR                    ║
║                                                              ║
╚══════════════════════════════════════════════════════════════╝
    """)

def send_transaction_via_curl(node_port, from_addr, to_addr, amount):
    """Отправляет транзакцию через curl (HTTP API)"""
    import subprocess
    cmd = f'curl -s -X POST http://localhost:{node_port + 1000}/transaction -H "Content-Type: application/json" -d \'{{"from":"{from_addr}","to":"{to_addr}","amount":{amount}}}\''
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=2)
        return "OK" in result.stdout
    except:
        return False

def send_transaction(node_url, from_addr, to_addr, amount):
    """Отправляет транзакцию через HTTP API"""
    tx_data = {
        "from": from_addr,
        "to": to_addr,
        "amount": round(amount, 2),
        "fee": 0.001
    }
    
    # Извлекаем порт из URL
    port = int(node_url.split(":")[-1])
    api_port = port + 1000
    
    try:
        response = requests.post(
            f"http://localhost:{api_port}/transaction",
            json=tx_data,
            timeout=2
        )
        return response.status_code == 200
    except:
        return False


def worker(worker_id, duration):
    """Рабочий поток"""
    local_count = 0
    local_success = 0
    end_time = time.time() + duration if duration > 0 else float('inf')
    
    while time.time() < end_time:
        node = random.choice(NODES)
        from_addr = random.choice(ADDRESSES)
        to_addr = random.choice(ADDRESSES)
        while from_addr == to_addr:
            to_addr = random.choice(ADDRESSES)
        amount = random.uniform(0.1, 100)
        
        success = send_transaction(node, from_addr, to_addr, amount)
        
        local_count += 1
        if success:
            local_success += 1
        
        # Обновляем глобальную статистику
        STATS["total"] += 1
        if success:
            STATS["success"] += 1
        else:
            STATS["failed"] += 1
        
        # Вывод прогресса каждые 10 транзакций
        if STATS["total"] % 10 == 0:
            elapsed = time.time() - STATS["start_time"]
            tps = STATS["total"] / elapsed if elapsed > 0 else 0
            success_rate = (STATS["success"] / STATS["total"] * 100) if STATS["total"] > 0 else 0
            print(f"\r📊 [{datetime.now().strftime('%H:%M:%S')}] "
                  f"Транзакций: {STATS['total']} | "
                  f"Успешно: {STATS['success']} | "
                  f"TPS: {tps:.1f} | "
                  f"Успешность: {success_rate:.1f}%", end="", flush=True)
        
        time.sleep(random.uniform(0.5, 2))
    
    return local_count, local_success

def main():
    print_banner()
    print(f"⚙️  Конфигурация:")
    print(f"   • Нод в сети: {len(NODES)}")
    print(f"   • Адресов: {len(ADDRESSES)}")
    print(f"   • HTTP API порты: 9000-9005")
    print()
    
    try:
        duration = int(input("⏱️  Время работы (секунд, 0 - бесконечно): ") or "0")
    except:
        duration = 0
    
    num_workers = int(input("🔧 Количество потоков (1-10): ") or "4")
    num_workers = max(1, min(10, num_workers))
    
    print(f"\n🚀 Запуск {num_workers} потоков...")
    print("─" * 70)
    
    threads = []
    
    # Запуск рабочих потоков
    for i in range(num_workers):
        t = threading.Thread(target=worker, args=(i, duration))
        t.daemon = True
        t.start()
        threads.append(t)
    
    try:
        if duration > 0:
            print(f"\n⏰ Работа в течение {duration} секунд...")
            time.sleep(duration)
            print(f"\n⏰ Время вышло")
        else:
            print("\n🔄 Генерация... Нажми Ctrl+C для остановки")
            while True:
                time.sleep(1)
    except KeyboardInterrupt:
        print("\n\n🛑 Остановка генератора...")
    finally:
        elapsed = time.time() - STATS["start_time"]
        print("\n" + "═" * 70)
        print("📊 ИТОГОВАЯ СТАТИСТИКА")
        print("═" * 70)
        print(f"⏱️  Время работы: {elapsed:.1f} сек")
        print(f"📦 Всего транзакций: {STATS['total']}")
        print(f"✅ Успешно: {STATS['success']}")
        print(f"❌ Ошибок: {STATS['failed']}")
        if STATS['total'] > 0:
            print(f"📈 Средний TPS: {STATS['total']/elapsed:.2f}")
            print(f"🎯 Успешность: {STATS['success']/STATS['total']*100:.1f}%")
        print("═" * 70)

if __name__ == "__main__":
    main()
