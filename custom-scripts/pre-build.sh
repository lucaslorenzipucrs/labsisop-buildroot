#!/bin/sh

# Cria as pastas de destino se elas nao existirem
mkdir -p $BASE_DIR/target/etc/init.d
mkdir -p $BASE_DIR/target/usr/bin

# Copia o script de rede (ja existente) e da permissao
cp $BASE_DIR/../custom-scripts/S41network-config $BASE_DIR/target/etc/init.d
chmod +x $BASE_DIR/target/etc/init.d/S41network-config

# Copia o SystemInfo (systeminfo.py) para /usr/bin no target
cp $BASE_DIR/../custom-scripts/systeminfo.py $BASE_DIR/target/usr/bin/
chmod +x $BASE_DIR/target/usr/bin/systeminfo.py

# Copia o script de init que sobe o servidor systeminfo automaticamente
cp $BASE_DIR/../custom-scripts/S99systeminfo $BASE_DIR/target/etc/init.d/
chmod +x $BASE_DIR/target/etc/init.d/S99systeminfo
