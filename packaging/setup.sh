#!/usr/bin/env bash
# setup.sh - Setup completo para Naruto Rise of a Ninja Recomp em Linux
# Este script configura o ambiente, baixa dependências e prepara o projeto

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_DIR"

echo "=== Naruto: Rise of a Ninja - Recomp Linux Setup ==="
echo ""

# Verificar se já foi configurado
if [ -d "sdk/out/install/linux-amd64" ] && [ -f "app/out/build/linux-amd64-release/narutorise_launcher" ]; then
    echo "✅ Ambiente já configurado anteriormente."
    echo "Rode './narutorise_launcher' para iniciar ou './packaging/install-linux.sh' para instalar o ISO."
    exit 0
fi

echo "=== Etapa 1: Verificando dependências do sistema ==="

# Verificar dependências básicas
REQ_TOOLS="cmake clang ninja build-essential"
MISSING=""

for tool in $REQ_TOOLS; do
    if ! command -v $tool &> /dev/null; then
        MISSING="$MISSING $tool"
    fi
done

if [ -n "$MISSING" ]; then
    echo "❌ Ferramentas faltando:$MISSING"
    echo "Instale-as com: sudo apt install cmake clang ninja-build build-essential"
    exit 1
fi

echo "✅ Dependências do sistema OK"

echo ""
echo "=== Etapa 2: Baixando extract-xiso Linux ==="

EXTRACT_XISO="$PROJECT_DIR/tools/extract-xiso/extract-xiso"
if [ ! -x "$EXTRACT_XISO" ]; then
    echo "Baixando extract-xiso para Linux..."
    DOWNLOAD_URL="https://github.com/XboxDev/extract-xiso/releases/latest/download/extract-xiso_Linux.zip"
    mkdir -p "$PROJECT_DIR/tools/extract-xiso"
    if curl -fL "$DOWNLOAD_URL" -o "$PROJECT_DIR/tools/extract-xiso/extract-xiso_Linux.zip" 2>/dev/null; then
        unzip -o "$PROJECT_DIR/tools/extract-xiso/extract-xiso_Linux.zip" -d "$PROJECT_DIR/tools/extract-xiso/"
        chmod +x "$EXTRACT_XISO"
        echo "✓ extract-xiso Linux baixado."
    else
        echo "⚠️ Falha ao baixar extract-xiso automaticamente."
        echo "Por favor, baixe manualmente de: https://github.com/XboxDev/extract-xiso"
        exit 1
    fi
else
    echo "✓ extract-xiso já disponível."
fi

echo ""
echo "=== Etapa 3: Build do SDK ==="

# Build do SDK
if [ ! -d "sdk/out/build/linux-amd64" ]; then
    echo "Configurando SDK..."
    env -i HOME="$HOME" PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin LANG=C.UTF-8 cmake --preset linux-amd64 -S sdk -B sdk/out/build/linux-amd64 2>&1 | tail -5
fi

echo "Buildando SDK..."
env -i HOME="$HOME" PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin LANG=C.UTF-8 cmake --build sdk/out/build/linux-amd64 --config Release --target install 2>&1 | tail -5

echo ""
echo "=== Etapa 4: Build do Launcher ==="

# Build do launcher
if [ ! -f "app/out/build/linux-amd64-release/narutorise_launcher" ]; then
    echo "Configurando launcher..."
    env -i HOME="$HOME" PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin LANG=C.UTF-8 cmake --preset linux-amd64-release -S app -B app/out/build/linux-amd64-release -DCMAKE_PREFIX_PATH="$PWD/sdk/out/install/linux-amd64" 2>&1 | tail -5
fi

echo "Buildando launcher..."
env -i HOME="$HOME" PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin LANG=C.UTF-8 cmake --build app/out/build/linux-amd64-release --target narutorise_launcher -j$(nproc) 2>&1 | tail -5

echo ""
echo "=== Setup Concluído ==="
echo ""
echo "O que foi feito:"
echo "1. ✅ Dependências do sistema verificadas"
echo "2. ✅ extract-xiso Linux baixado"
echo "3. ✅ SDK compilado e instalado"
echo "4. ✅ Launcher compilado em app/out/build/linux-amd64-release/narutorise_launcher"
echo ""
echo "Próximos passos:"
echo "1. Rode './packaging/install-linux.sh' e aponta o ISO do Xbox 360"
echo "2. Ou rode './narutorise_launcher' e selecione a pasta game_root/"
echo ""
echo "Divirta-se! (Port em desenvolvimento, espere bugs)"
