#!/bin/sh

# Cria as pastas de destino se elas não existirem
mkdir -p $BASE_DIR/target/etc/init.d

# Copia o arquivo e dá permissão
cp $BASE_DIR/../custom-scripts/S41network-config $BASE_DIR/target/etc/init.d
chmod +x $BASE_DIR/target/etc/init.d/S41network-config
