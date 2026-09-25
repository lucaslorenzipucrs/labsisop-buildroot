#!/usr/bin/env python3
"""
Analisa o log do escalonador SSTF (linhas "[SSTF] add ..." / "[SSTF] dsp ...")
capturado do dmesg dentro do QEMU, e gera o relatorio de desempenho pedido
na etapa 5 do trabalho (Ordem de chegada x Ordem de atendimento, distancia
total percorrida pela cabeca do disco, e os graficos correspondentes).

Como usar
---------
1. Dentro do QEMU, depois de rodar o teste (sector_read) com o scheduler
   sstf selecionado, rode:

       # dmesg | grep "\[SSTF\]"

   e copie TODA a saida (pode ter milhares de linhas, sem problema).

2. Na sua maquina host (fora do QEMU), cole essa saida dentro de um
   arquivo de texto, por exemplo "sstf_log.txt".

3. Rode este script (precisa de python3 + matplotlib no host; se nao
   tiver: pip install matplotlib):

       $ python3 analyze_sstf_log.py sstf_log.txt

   Isso gera:
     - um resumo no terminal (numero de requisicoes, distancia total
       percorrida na ordem de chegada vs. na ordem de atendimento, e o
       ganho percentual do SSTF)
     - sstf_log_grafico.png: dois graficos lado a lado (ordem de chegada
       e ordem de atendimento), no mesmo estilo do exemplo do enunciado
     - sstf_log_dados.csv: tabela com as duas ordens, pra anexar no
       relatorio

Tambem da pra comparar dois logs (ex.: um com o disco no "sstf" e outro
no "noop"/"cfq" pra servir de baseline) passando os dois arquivos:

       $ python3 analyze_sstf_log.py sstf_log.txt --compare noop_log.txt
"""
import argparse
import csv
import re
import sys
from pathlib import Path

LINE_RE = re.compile(r"\[SSTF\]\s+(add|dsp)\s+([RW])\s+(\d+)")


def parse_log(path):
    """Retorna (ordem_chegada, ordem_atendimento), cada uma uma lista de
    (setor, direcao) na ordem em que apareceram no log."""
    arrival = []
    dispatch = []
    with open(path, "r", errors="ignore") as f:
        for line in f:
            m = LINE_RE.search(line)
            if not m:
                continue
            kind, direction, sector = m.group(1), m.group(2), int(m.group(3))
            if kind == "add":
                arrival.append((sector, direction))
            else:
                dispatch.append((sector, direction))
    return arrival, dispatch


def total_seek_distance(order, start_head=0):
    """Soma |setor_atual - setor_anterior| percorrendo a lista na ordem
    dada, partindo de start_head (simula o deslocamento total da cabeca
    do disco)."""
    head = start_head
    total = 0
    for sector, _ in order:
        total += abs(sector - head)
        head = sector
    return total


def print_summary(label, arrival, dispatch):
    print(f"\n=== {label} ===")
    print(f"Requisicoes adicionadas (chegada): {len(arrival)}")
    print(f"Requisicoes despachadas (atendimento): {len(dispatch)}")

    if len(arrival) != len(dispatch):
        print("AVISO: numero de 'add' e 'dsp' diferente - log pode estar "
              "incompleto (rode 'dmesg | grep \"[SSTF]\"' de novo depois "
              "que o teste terminar por completo).")

    dist_chegada = total_seek_distance(arrival)
    dist_atendimento = total_seek_distance(dispatch)

    print(f"Distancia total se atendesse na ORDEM DE CHEGADA (~FIFO): "
          f"{dist_chegada:,} setores".replace(",", "."))
    print(f"Distancia total na ORDEM DE ATENDIMENTO real (SSTF): "
          f"{dist_atendimento:,} setores".replace(",", "."))

    if dist_chegada > 0:
        ganho = 100.0 * (1 - dist_atendimento / dist_chegada)
        print(f"Reducao obtida pelo SSTF: {ganho:.1f}%")


def plot_orders(arrival, dispatch, out_png, title_prefix=""):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

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
    print(f"\nGrafico salvo em: {out_png}")


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
    print(f"Tabela salva em: {out_csv}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logfile", help="arquivo de texto com a saida do "
                     "'dmesg | grep \"[SSTF]\"' colada do QEMU")
    ap.add_argument("--compare", metavar="OUTRO_LOG", help="opcional: "
                     "outro log (ex.: rodado com noop/cfq) para comparar "
                     "a distancia total percorrida")
    ap.add_argument("--out-prefix", default=None, help="prefixo dos "
                     "arquivos de saida (padrao: nome do logfile sem "
                     "extensao)")
    args = ap.parse_args()

    log_path = Path(args.logfile)
    if not log_path.exists():
        sys.exit(f"Arquivo nao encontrado: {log_path}")

    arrival, dispatch = parse_log(log_path)
    if not arrival and not dispatch:
        sys.exit("Nenhuma linha '[SSTF] add/dsp' encontrada nesse arquivo "
                  "- confira se voce colou a saida do 'dmesg | grep \"[SSTF]\"' "
                  "certa (e nao a tela inteira do dmesg).")

    prefix = args.out_prefix or log_path.stem
    print_summary("SSTF", arrival, dispatch)
    plot_orders(arrival, dispatch, f"{prefix}_grafico.png")
    save_csv(arrival, dispatch, f"{prefix}_dados.csv")

    if args.compare:
        cmp_path = Path(args.compare)
        if not cmp_path.exists():
            sys.exit(f"Arquivo de comparacao nao encontrado: {cmp_path}")
        cmp_arrival, cmp_dispatch = parse_log(cmp_path)
        print_summary(f"Comparacao ({cmp_path.name})", cmp_arrival, cmp_dispatch)

        dist_sstf = total_seek_distance(dispatch)
        dist_cmp = total_seek_distance(cmp_dispatch)
        print(f"\n=== Resumo comparativo ===")
        print(f"Distancia total (SSTF, {log_path.name}): "
              f"{dist_sstf:,} setores".replace(",", "."))
        print(f"Distancia total ({cmp_path.name}): "
              f"{dist_cmp:,} setores".replace(",", "."))
        if dist_cmp > 0:
            ganho = 100.0 * (1 - dist_sstf / dist_cmp)
            print(f"SSTF percorreu {ganho:.1f}% menos setores que "
                  f"{cmp_path.name}")


if __name__ == "__main__":
    main()
