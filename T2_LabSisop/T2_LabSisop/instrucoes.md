# Trabalho Pratico 2 - Escalonador SSTF

Implementamos um escalonador de disco para o Linux 4.13.9 usando a politica
SSTF (Shortest Seek Time First). O modulo seleciona, entre as requisicoes
pendentes, a que esta no setor mais proximo da ultima requisicao atendida.

O projeto possui tres partes principais:

- `sstf-iosched.c`: modulo do escalonador;
- `sector_read.c`: programa de teste que gera leituras concorrentes em
  `/dev/sdb` usando `fork()`;
- `analyze_sstf_log.py`: script que calcula os deslocamentos e gera a tabela
  e o grafico usados no relatorio.

## Compilacao

Os comandos abaixo devem ser executados no host/Codespace, considerando que
o diretorio do Buildroot seja `~/linuxdistro/buildroot`.

```bash
cd ~/linuxdistro/buildroot/sstf-iosched
make

cd ..
make
```

## Execucao

Se o arquivo `sdb.bin` ainda nao existir, crie o disco de testes uma vez:

```bash
cd ~/linuxdistro/buildroot
dd if=/dev/zero of=sdb.bin bs=512 count=2097152
```

Inicie o QEMU com o disco adicional:

```bash
qemu-system-i386 --kernel output/images/bzImage \
  --hda output/images/rootfs.ext2 \
  --hdb sdb.bin \
  --nographic \
  --append "console=ttyS0 root=/dev/sda"
```

No terminal do QEMU, carregue o modulo, ative o scheduler e execute o teste:

```sh
modprobe sstf-iosched
echo sstf > /sys/block/sdb/queue/scheduler

echo 2 > /sys/block/sdb/queue/nomerges
blockdev --setra 0 /dev/sdb
dmesg -C
echo 3 > /proc/sys/vm/drop_caches

sector_read
```

Depois que `sector_read` terminar, salve o log do modulo:

```sh
dmesg | grep '\[SSTF\]' > /tmp/sstf_log.txt
```

Copie o conteudo de `/tmp/sstf_log.txt` para um arquivo `sstf_log.txt` no
host. Para sair do QEMU, use `Ctrl+A` e depois `X`.

## Analise dos resultados

No host/Codespace, execute:

```bash
python3 analyze_sstf_log.py sstf_log.txt
```

O comando sempre gera `sstf_log_dados.csv`. Para gerar tambem o grafico,
instale o matplotlib

```bash
python3 -m pip install --user matplotlib
python3 analyze_sstf_log.py sstf_log.txt
```