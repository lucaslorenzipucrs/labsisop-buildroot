#!/usr/bin/env python3
"""
SystemInfo - Servidor HTTP para exportação de informação do sistema
Laboratório de Sistemas Operacionais (CC) - Trabalho Prático

Todas as informações são obtidas dinamicamente, a cada requisição,
exclusivamente a partir de /proc e /sys, usando apenas a Python
Standard Library (sem dependências externas).
"""

import json
import os
import re
import socket
import struct
import fcntl
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from datetime import datetime

# --- Funções de coleta de informação --- #


def get_datetime():
    """Data e hora atual do sistema (lida do relógio do kernel)."""
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def get_uptime():
    """Tempo desde o último boot, em segundos, lido de /proc/uptime."""
    try:
        with open("/proc/uptime", "r") as f:
            uptime_seconds = float(f.readline().split()[0])
        return int(uptime_seconds)
    except Exception:
        return 0


def _read_cpuinfo():
    """Lê e faz o parsing de /proc/cpuinfo em uma lista de dicts (um por cpu)."""
    cpus = []
    current = {}
    try:
        with open("/proc/cpuinfo", "r") as f:
            for line in f:
                line = line.strip()
                if not line:
                    if current:
                        cpus.append(current)
                        current = {}
                    continue
                if ":" in line:
                    key, _, value = line.partition(":")
                    current[key.strip()] = value.strip()
        if current:
            cpus.append(current)
    except Exception:
        pass
    return cpus


def _cpu_model_name(cpus):
    if not cpus:
        return "unknown"
    first = cpus[0]
    # x86: "model name"; ARM 32 bits antigos: "Hardware"/"Processor";
    # ARM64: geralmente não tem nome amigável, usamos "CPU part"/"CPU implementer".
    for key in ("model name", "Processor", "Hardware", "cpu model"):
        if key in first:
            return first[key]
    if "CPU part" in first:
        implementer = first.get("CPU implementer", "?")
        part = first.get("CPU part", "?")
        return "ARM CPU implementer={} part={}".format(implementer, part)
    return "unknown"


def _cpu_speed_mhz(cpus):
    # 1) tenta /sys (frequência atual reportada pelo cpufreq)
    for path in (
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq",
        "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq",
    ):
        try:
            with open(path, "r") as f:
                khz = int(f.read().strip())
                return round(khz / 1000)
        except Exception:
            continue
    # 2) tenta /proc/cpuinfo ("cpu MHz", comum em x86)
    if cpus and "cpu MHz" in cpus[0]:
        try:
            return round(float(cpus[0]["cpu MHz"]))
        except Exception:
            pass
    return 0


def _cpu_usage_percent(sample_seconds=0.1):
    """Calcula o uso de CPU amostrando /proc/stat duas vezes."""
    def read_stat():
        with open("/proc/stat", "r") as f:
            line = f.readline()
        parts = line.split()
        values = list(map(int, parts[1:]))
        idle = values[3] + (values[4] if len(values) > 4 else 0)
        total = sum(values)
        return idle, total

    try:
        idle1, total1 = read_stat()
        time.sleep(sample_seconds)
        idle2, total2 = read_stat()
        idle_delta = idle2 - idle1
        total_delta = total2 - total1
        if total_delta <= 0:
            return 0.0
        usage = (1.0 - idle_delta / total_delta) * 100.0
        return round(max(0.0, min(100.0, usage)), 1)
    except Exception:
        return 0.0


def get_cpu_info():
    cpus = _read_cpuinfo()
    return {
        "model": _cpu_model_name(cpus),
        "speed_mhz": _cpu_speed_mhz(cpus),
        "usage_percent": _cpu_usage_percent(),
    }


def get_memory_info():
    """Memória total/usada (MB), lida de /proc/meminfo."""
    meminfo = {}
    try:
        with open("/proc/meminfo", "r") as f:
            for line in f:
                key, _, rest = line.partition(":")
                value_kb = rest.strip().split()[0]
                meminfo[key.strip()] = int(value_kb)
    except Exception:
        return {"total_mb": 0, "used_mb": 0}

    total_kb = meminfo.get("MemTotal", 0)
    # MemAvailable é a estimativa mais precisa (kernel >= 3.14).
    if "MemAvailable" in meminfo:
        available_kb = meminfo["MemAvailable"]
    else:
        available_kb = (
            meminfo.get("MemFree", 0)
            + meminfo.get("Buffers", 0)
            + meminfo.get("Cached", 0)
        )
    used_kb = max(0, total_kb - available_kb)
    return {
        "total_mb": round(total_kb / 1024),
        "used_mb": round(used_kb / 1024),
    }


def get_os_version():
    """Versão do sistema operacional, lida de /proc/version."""
    try:
        with open("/proc/version", "r") as f:
            return f.read().strip()
    except Exception:
        return "unknown"


def get_process_list():
    """Lista de processos ({pid, name}), lida de /proc/<pid>/comm."""
    processes = []
    try:
        for entry in os.listdir("/proc"):
            if not entry.isdigit():
                continue
            pid = int(entry)
            try:
                with open("/proc/{}/comm".format(pid), "r") as f:
                    name = f.read().strip()
            except Exception:
                # processo pode ter terminado entre o listdir e a leitura
                continue
            processes.append({"pid": pid, "name": name})
    except Exception:
        pass
    processes.sort(key=lambda p: p["pid"])
    return processes


def get_disks():
    """Dispositivos de armazenamento ({device, size_mb}), lidos de /sys/class/block."""
    disks = []
    base = "/sys/class/block"
    try:
        for name in sorted(os.listdir(base)):
            dev_path = os.path.join(base, name)
            # ignora partições (possuem o arquivo "partition")
            if os.path.exists(os.path.join(dev_path, "partition")):
                continue
            # ignora dispositivos virtuais irrelevantes (loop, ram, dm sem uso)
            if re.match(r"^(loop|ram)\d*$", name):
                continue
            size_path = os.path.join(dev_path, "size")
            try:
                with open(size_path, "r") as f:
                    sectors = int(f.read().strip())
            except Exception:
                continue
            if sectors == 0:
                continue
            size_mb = round((sectors * 512) / (1024 * 1024))
            disks.append({"device": "/dev/{}".format(name), "size_mb": size_mb})
    except Exception:
        pass
    return disks


def get_usb_devices():
    """Dispositivos USB ({port, description}), lidos de /sys/bus/usb/devices."""
    devices = []
    base = "/sys/bus/usb/devices"
    if not os.path.isdir(base):
        return devices

    def read_attr(path):
        try:
            with open(path, "r") as f:
                return f.read().strip()
        except Exception:
            return ""

    for entry in sorted(os.listdir(base)):
        dev_path = os.path.join(base, entry)
        # interfaces têm nomes como "1-1:1.0" (contêm ":"); ignoramos, só
        # queremos os dispositivos em si ("1-1", "2-3.1", "usb1", ...).
        if ":" in entry:
            continue
        id_vendor = read_attr(os.path.join(dev_path, "idVendor"))
        id_product = read_attr(os.path.join(dev_path, "idProduct"))
        if not id_vendor or not id_product:
            continue
        manufacturer = read_attr(os.path.join(dev_path, "manufacturer"))
        product = read_attr(os.path.join(dev_path, "product"))
        if manufacturer and product:
            description = "{} {}".format(manufacturer, product)
        elif product:
            description = product
        else:
            description = "USB device {}:{}".format(id_vendor, id_product)
        devices.append({"port": entry, "description": description})
    return devices


def _get_ipv4_address(ifname):
    """Obtém o endereço IPv4 de uma interface via ioctl SIOCGIFADDR (stdlib)."""
    SIOCGIFADDR = 0x8915
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            packed_iface = struct.pack("256s", ifname[:15].encode("utf-8"))
            packed_addr = fcntl.ioctl(s.fileno(), SIOCGIFADDR, packed_iface)
            return socket.inet_ntoa(packed_addr[20:24])
        finally:
            s.close()
    except Exception:
        return ""


def get_network_adapters():
    """Interfaces de rede ({interface, ip_address}), lidas de /sys/class/net."""
    adapters = []
    base = "/sys/class/net"
    try:
        for ifname in sorted(os.listdir(base)):
            ip_address = _get_ipv4_address(ifname)
            adapters.append({"interface": ifname, "ip_address": ip_address})
    except Exception:
        pass
    return adapters


# --- Servidor HTTP --- #


class StatusHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/status":
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"Not Found")
            return

        response = {
            "datetime": get_datetime(),
            "uptime_seconds": get_uptime(),
            "cpu": get_cpu_info(),
            "memory": get_memory_info(),
            "os_version": get_os_version(),
            "processes": get_process_list(),
            "disks": get_disks(),
            "usb_devices": get_usb_devices(),
            "network_adapters": get_network_adapters(),
        }

        data = json.dumps(response, indent=2).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, format, *args):
        # Log simples no stdout (visível no console serial do sistema embarcado)
        print("[systeminfo] %s - %s" % (self.address_string(), format % args))


def run_server(port=8080):
    print("Servidor disponível em http://0.0.0.0:{}/status".format(port))
    server = HTTPServer(("0.0.0.0", port), StatusHandler)
    server.serve_forever()


if __name__ == "__main__":
    run_server()
