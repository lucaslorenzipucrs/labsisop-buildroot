#!/bin/sh

# Cria as pastas de destino se elas não existirem
mkdir -p $BASE_DIR/target/etc/init.d

# Copia o arquivo e dá permissão
if [ -f "$BASE_DIR/../custom-scripts/S41network-config" ]; then
    cp "$BASE_DIR/../custom-scripts/S41network-config" "$BASE_DIR/target/etc/init.d"
    chmod +x "$BASE_DIR/target/etc/init.d/S41network-config"
else
    echo "O arquivo de configuração da rede não existe."
fi

