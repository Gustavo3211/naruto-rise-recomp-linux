#!/usr/bin/env bash
# package-release.sh - Instala o Naruto Rise of a Ninja Recomp em Linux
# Prepara o game_root a partir do ISO do Xbox 360

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_DIR"

echo "=== Naruto: Rise of a Ninja - Recomp Linux Installer ==="
echo ""

# Verificar dependências
if ! command -v cmake &> /dev/null; then
    echo "❌ CMake não encontrado. Instale-o antes de continuar."
    exit 1
fi

if ! command -v clang &> /dev/null; then
    echo "❌ Clang não encontrado. Instale-o antes de continuar."
    exit 1
fi

# Solicitar o ISO do jogo
echo "Por favor, forneça o caminho para o ISO do Naruto: Rise of a Ninja (Xbox 360)."
read -r -p "Caminho do ISO [Enter para pular]: " ISO_PATH

if [ -z "$ISO_PATH" ]; then
    echo "⚠️ ISO não fornecida. O script tentará baixar o extract-xiso Linux automaticamente."
    ISO_PATH=""
fi

# Baixar extract-xiso para Linux se necessário
EXTRACT_XISO="$PROJECT_DIR/tools/extract-xiso/extract-xiso"
if [ ! -x "$EXTRACT_XISO" ]; then
    echo "Baixando extract-xiso para Linux..."
    DOWNLOAD_URL="https://github.com/XboxDev/extract-xiso/releases/latest/download/extract-xiso_Linux.zip"
    mkdir -p "$PROJECT_DIR/tools/extract-xiso"
    if curl -fL "$DOWNLOAD_URL" -o "$PROJECT_DIR/tools/extract-xiso/extract-xiso_Linux.zip" 2>/dev/null; then
        unzip -o "$PROJECT_DIR/tools/extract-xiso/extract-xiso_Linux.zip" -d "$PROJECT_DIR/tools/extract-xiso/"
        chmod +x "$EXTRACT_XISO"
        echo "✓ extract-xiso Linux baixado e instalado."
    else
        echo "⚠️ Não foi possível baixar extract-xiso automaticamente."
        echo "Fazendo fallback para Wine ou extração manual."
        EXTRACT_XISO=""
    fi
fi

# Se ainda não tiver extract-xiso, tentar Wine
if [ ! -x "$EXTRACT_XISO" ] && command -v wine &> /dev/null; then
    echo "extract-xiso não encontrado nativamente. Tentando via Wine..."
    EXTRACT_XISO="wine $PROJECT_DIR/tools/extract-xiso/extract-xiso.exe"
    if [ ! -f "$PROJECT_DIR/tools/extract-xiso/extract-xiso.exe" ]; then
        echo "⚠️ Arquivo .exe do extract-xiso também não encontrado."
        EXTRACT_XISO=""
    fi
fi

# Solicionar o ISO do jogo
if [ -z "$ISO_PATH" ]; then
    echo ""
    echo "Por favor, forneça o caminho para o ISO do Naruto: Rise of a Ninja (Xbox 360)."
    read -r -p "Caminho do ISO: " ISO_PATH
fi

if [ ! -f "$ISO_PATH" ]; then
    echo "❌ Arquivo ISO não encontrado em: $ISO_PATH"
    echo ""
    echo "Arquivos necessários após a extração:"
    echo "  - default.xex"
    echo "  - ai2c.dll"
    echo "  - (Opcional) ai2c1.dll, ai2c2.dll, ai2c3.dll para DLCs"
    exit 1
fi

# Pasta de destino
GAME_ROOT="$PROJECT_DIR/game_root"
if [ -d "$GAME_ROOT" ]; then
    echo "⚠️ Pasta game_root/ já existe. Vou limpá-la e reextrair."
    rm -rf "$GAME_ROOT"
fi
mkdir -p "$GAME_ROOT"

# Extrair
echo "Extraindo ISO com extract-xiso..."
if [ -x "$EXTRACT_XISO" ] && [ "$EXTRACT_XISO" != "wine" ]; then
    "$EXTRACT_XISO" -x -d "$GAME_ROOT" "$ISO_PATH" 2>/dev/null || \
    echo "⚠️ Falha com extract-xiso nativo, tentando via Wine..." && \
    wine "$PROJECT_DIR/tools/extract-xiso/extract-xiso.exe" -x -d "$GAME_ROOT" "$ISO_PATH" 2>/dev/null || \
    echo "⚠️ Falha na extração. Verifique o ISO ou o extract-xiso."
elif [ "$EXTRACT_XISO" = "wine" ]; then
    wine "$PROJECT_DIR/tools/extract-xiso/extract-xiso.exe" -x -d "$GAME_ROOT" "$ISO_PATH" 2>/dev/null || \
    echo "⚠️ Falha na extração via Wine."
else
    echo "❌ Nenhum extract-xiso disponível. Extração manual necessária."
    echo "Por favor, extraia o ISO manualmente e copie estes arquivos para $GAME_ROOT/:"
    echo "  - default.xex"
    echo "  - ai2c.dll"
    echo "  - (Opcional) ai2c1.dll, ai2c2.dll, ai2c3.dll"
fi

# Verificar se os arquivos essenciais existem
if [ ! -f "$GAME_ROOT/default.xex" ]; then
    echo ""
    echo "⚠️ Arquivo essential 'default.xex' não encontrado em $GAME_ROOT/"
    echo "O launcher não poderá iniciar o jogo sem este arquivo."
fi

echo ""
echo "=== Instalação concluída ==="
echo ""
echo "Próximos passos:"
echo "1. Certifique-se de que os arquivos do jogo estão em $GAME_ROOT/"
echo "2. Execute o launcher: ./narutorise_launcher"
echo "3. No launcher, a pasta game_root/ já será detectada"
echo "4. Divirta-se! (Port em desenvolvimento, espere bugs)"
echo ""
