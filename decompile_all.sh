#!/bin/bash
# Script para decompilar clases.dex con Jadx y archivos .so con devrvk/so-decompiler usando Docker.
# Creado automáticamente como una skill de IA.

echo "Iniciando decompilación con JADX..."
docker run --rm -v "$(pwd):/app" ubuntu:latest bash -c "
  cd /app
  apt-get update && apt-get install -y wget unzip default-jre
  wget -qO- https://github.com/skylot/jadx/releases/download/v1.4.7/jadx-1.4.7.zip > jadx.zip
  unzip -q jadx.zip -d jadx
  ./jadx/bin/jadx -d /app/apk_decompiled /app/ux0_data/popclassic/original/classes.dex
  rm -rf jadx jadx.zip
"
echo "JADX Finalizado. Resultados en la carpeta apk_decompiled/"

echo "Iniciando decompilación de .so con Ghidra y Angr (so-decompiler)..."
# Descompilar libgame_logic.so
docker run --rm --platform linux/amd64 -v "$(pwd):/app" devrvk/so-decompiler decompile /app/bin/libgame_logic.so /app/so_decompiled/libgame_logic

# Descompilar libcocos2d.so
docker run --rm --platform linux/amd64 -v "$(pwd):/app" devrvk/so-decompiler decompile /app/bin/libcocos2d.so /app/so_decompiled/libcocos2d

# Descompilar libcocosdenshion.so
docker run --rm --platform linux/amd64 -v "$(pwd):/app" devrvk/so-decompiler decompile /app/bin/libcocosdenshion.so /app/so_decompiled/libcocosdenshion

echo "Decompilación de archivos .so finalizada. Resultados en so_decompiled/"
