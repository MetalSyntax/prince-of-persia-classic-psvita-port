#!/bin/bash
set -e

# Configuración
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="/tmp/popclassic-build"
SRC_DIR="/tmp/popclassic-src"
VPK_NAME="popclassic.vpk"

echo "================================================================"
echo "  🚀 Script de Build Automático para Prince of Persia (PS Vita)"
echo "================================================================"

echo "[1/4] Preparando entorno de compilación..."
# Evitamos problemas de rutas con espacios usando un directorio temporal en /tmp
mkdir -p "$BUILD_DIR"
mkdir -p "$SRC_DIR"

# Asegurar que VITASDK está definido
if [ -z "$VITASDK" ]; then
    if [ -d "/usr/local/vitasdk" ]; then
        export VITASDK="/usr/local/vitasdk"
    elif [ -d "$HOME/vitasdk" ]; then
        export VITASDK="$HOME/vitasdk"
    else
        echo "❌ Error: La variable de entorno VITASDK no está definida y no se encontró en rutas por defecto."
        exit 1
    fi
    export PATH="$VITASDK/bin:$PATH"
fi

# Sincronizamos el código fuente (excluyendo el historial y builds viejos)
rsync -a --exclude '.git' --exclude 'build' --exclude '.*' "$PROJECT_DIR/" "$SRC_DIR/"

echo "[2/4] Ejecutando CMake y Make..."
cd "$BUILD_DIR"
echo "Seleccione el objetivo de compilación:"
echo "1) PSVita (Hardware Real)"
echo "2) Vita3k (Emulador)"
read -p "Opción [1/2] (por defecto 2): " TARGET_OPTION

if [ "$TARGET_OPTION" == "1" ]; then
    echo "[!] Configurando para PSVita (EMULATOR_BUILD=OFF)"
    CMAKE_FLAGS="-DEMULATOR_BUILD=OFF"
else
    echo "[!] Configurando para Vita3k (EMULATOR_BUILD=ON)"
    CMAKE_FLAGS="-DEMULATOR_BUILD=ON"
fi

cmake "$SRC_DIR" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release $CMAKE_FLAGS
make -j$(sysctl -n hw.ncpu)

echo "[3/4] Exportando VPK generado..."
mkdir -p "$PROJECT_DIR/build"
cp "$VPK_NAME" "$PROJECT_DIR/build/$VPK_NAME"

echo "✅ Build exitoso: $PROJECT_DIR/build/$VPK_NAME"

echo "[4/4] Instalación..."
VITA3K_APP="/Applications/Vita3K.app/Contents/MacOS/Vita3K"
if [ -x "$VITA3K_APP" ]; then
    read -p "¿Deseas instalar y ejecutar el VPK en Vita3K ahora? [s/N] " INSTALL_VITA3K
    if [[ "$INSTALL_VITA3K" =~ ^[sS]$ ]]; then
        echo "🎮 Instalando VPK y lanzando el emulador..."
        # Vita3K instala y ejecuta automáticamente el VPK cuando se le pasa por argumento
        # Forzamos el backend a OpenGL usando -B OpenGL para evitar el bug del swizzler en Vulkan
        "$VITA3K_APP" -B OpenGL "$PROJECT_DIR/build/$VPK_NAME" > /dev/null 2>&1 &
        echo "¡Listo! El juego se está abriendo con el backend OpenGL."
    else
        echo "Omitiendo instalación automática en Vita3K."
        echo "Puedes instalar el archivo $PROJECT_DIR/build/$VPK_NAME manualmente."
    fi
else
    echo "⚠️ Vita3K no encontrado en la ruta por defecto (/Applications/Vita3K.app)."
    echo "Puedes instalar el archivo $PROJECT_DIR/build/$VPK_NAME manualmente en tu emulador o consola."
fi
