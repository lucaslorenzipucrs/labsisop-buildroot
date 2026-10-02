"""Analisa um log SSTF e gera o resumo, o CSV e o grafico comparativo.

Uso: python3 analyze_sstf_log.py sstf_log.txt
"""
import argparse
import csv
import re
import sys
from pathlib import Path

LINE_RE = re.compile(r"\[SSTF\]\s+(add|dsp)\s+([RW])\s+(\d+)")


def parse_log(path):
    """Retorna os eventos de chegada e despacho, sem linhas duplicadas."""
    arrival = []
    dispatch = []
    seen = set()
    with open(path, "r", errors="ignore") as f:
        for raw_line in f:
            line = raw_line.rstrip("\n")
            m = LINE_RE.search(line)
            if not m:
                continue
            if line in seen:
                continue
            seen.add(line)
            kind, direction, sector = m.group(1), m.group(2), int(m.group(3))
            if kind == "add":
                arrival.append((sector, direction))
            else:
                dispatch.append((sector, direction))
    return arrival, dispatch


def total_seek_distance(order, start_head=0):
    """Soma as distancias entre setores consecutivos, a partir de start_head."""
    head = start_head
    total = 0
    for sector, _ in order:
        total += abs(sector - head)
        head = sector
    return total


def print_summary(label, arrival, dispatch):
    print(f"\nResultados: {label}")
    print(f"Requisicoes recebidas: {len(arrival)}")
    print(f"Requisicoes atendidas: {len(dispatch)}")

    if len(arrival) != len(dispatch):
        print("Atencao: as contagens diferem; confira se a captura do log terminou.")

    dist_chegada = total_seek_distance(arrival)
    dist_atendimento = total_seek_distance(dispatch)

    print(f"Distancia estimada na ordem de chegada (FIFO): "
          f"{dist_chegada:,} setores".replace(",", "."))
    print(f"Distancia estimada na ordem de atendimento (SSTF): "
          f"{dist_atendimento:,} setores".replace(",", "."))

    if dist_chegada > 0:
        ganho = 100.0 * (1 - dist_atendimento / dist_chegada)
        print(f"Reducao estimada com SSTF: {ganho:.1f}%")


def plot_orders(arrival, dispatch, out_png, title_prefix=""):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print(f"Matplotlib nao esta instalado; o grafico {out_png} nao foi gerado.")
        return

    fig, axes = plt.subplots(1, 2, figsize=(12, 6), sharey=True)

    for ax, order, subtitle in (
        (axes[0], arrival, "Ordem de chegada"),
        (axes[1], dispatch, "Ordem de atendimento (SSTF)"),
    ):
        idx = list(range(1, len(order) + 1))
        sectors = [s for s, _ in order]
        ax.plot(sectors, idx, marker="", linewidth=1)
        ax.set_title(f"{title_prefix}{subtitle}")
        ax.set_xlabel("Setor")
        ax.grid(True, alpha=0.3)

    axes[0].set_ylabel("Ordem (indice da requisicao)")
    fig.tight_layout()
    fig.savefig(out_png, dpi=150)
    print(f"Grafico salvo: {out_png}")


def save_csv(arrival, dispatch, out_csv):
    n = max(len(arrival), len(dispatch))
    with open(out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["indice", "setor_chegada", "direcao_chegada",
                    "setor_atendimento", "direcao_atendimento"])
        for i in range(n):
            a_sector, a_dir = arrival[i] if i < len(arrival) else ("", "")
            d_sector, d_dir = dispatch[i] if i < len(dispatch) else ("", "")
            w.writerow([i + 1, a_sector, a_dir, d_sector, d_dir])
    print(f"CSV salvo: {out_csv}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("logfile", help="arquivo com os eventos [SSTF] add/dsp")
    args = ap.parse_args()

    log_path = Path(args.logfile)
    if not log_path.exists():
        sys.exit(f"Erro: arquivo de log nao encontrado: {log_path}")

    arrival, dispatch = parse_log(log_path)
    if not arrival and not dispatch:
        sys.exit("Erro: o arquivo nao contem eventos [SSTF] add/dsp.")

    prefix = log_path.stem
    print_summary("SSTF", arrival, dispatch)
    save_csv(arrival, dispatch, f"{prefix}_dados.csv")
    plot_orders(arrival, dispatch, f"{prefix}_grafico.png")


if __name__ == "__main__":
    main()
