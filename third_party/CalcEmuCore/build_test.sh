#!/usr/bin/env bash
# Compila el nucleo portado y sus bancos de pruebas.
# Sin SDL, sin Lua, sin readline: solo g++ y la biblioteca estandar. Eso es
# precisamente lo que hara que compile tambien en Xcode para iOS.

# Inventario local canonico: D:\DevTools es la junction sin espacios/tildes. Agregar
# primero usr/bin permite usar mkdir y las utilidades basicas aun cuando el llamador
# sea PowerShell y no una terminal MSYS2 inicializada.
MSYS_ROOT=/d/DevTools/msys64
if [ -x "$MSYS_ROOT/usr/bin/sh.exe" ]; then
    export PATH="$MSYS_ROOT/usr/bin:$PATH"
fi

# No se usa `dirname` aqui a proposito. Cuando PowerShell lanza bash directamente,
# MSYS2 puede heredar un PATH puramente Windows y ni siquiera encuentra dirname antes
# de que tengamos oportunidad de preparar el toolchain. Una ruta absoluta entregada
# por PowerShell llega como D:\..., por lo que primero se normaliza con cygpath.
SOURCE_PATH=${BASH_SOURCE[0]}
case "$SOURCE_PATH" in
    *\\*) SOURCE_PATH=$(cygpath -u -- "$SOURCE_PATH") || exit 1 ;;
esac
SCRIPT_DIR=${SOURCE_PATH%/*}
if [ "$SCRIPT_DIR" = "$SOURCE_PATH" ]; then
    SCRIPT_DIR=.
fi
cd -- "$SCRIPT_DIR" || exit 1

# El g++ de MSYS2 resuelve sus bibliotecas a partir de la ruta con la que se le
# invoca. La instalacion real esta en un directorio con tilde, y el enlazador no
# sabe abrirlo: falla con "cannot find crt2.o" aunque todos los .o esten bien.
# D:\DevTools es una junction sin tildes al mismo sitio, asi que se usa esa.
#
# Ojo: hay que invocarlo con la ruta en forma de WINDOWS ("D:/DevTools/..."). Con la
# forma POSIX equivalente ("/d/DevTools/...") no sirve, porque MSYS2 la resuelve
# hasta el directorio real y volvemos a la ruta con tilde.
#
# Consecuencia: este script funciona desde Git Bash o desde Windows, pero NO desde
# una shell de login de MSYS2, que resuelve la junction siempre. Arreglarlo de verdad
# pide reinstalar MSYS2 en una ruta sin tildes, y eso es de la maquina, no del
# proyecto.
GPP=g++
if [ -x "$MSYS_ROOT/ucrt64/bin/g++.exe" ]; then
    GPP="D:/DevTools/msys64/ucrt64/bin/g++.exe"
    export PATH="$MSYS_ROOT/ucrt64/bin:$MSYS_ROOT/usr/bin:$PATH"
elif [ -x /ucrt64/bin/g++ ]; then
    GPP=/ucrt64/bin/g++
    export PATH="/usr/bin:/ucrt64/bin:$PATH"
fi

mkdir -p build/tmp
# Por el mismo motivo: el /tmp de MSYS2 tambien cae en la ruta con tilde y el
# ensamblador no puede escribir ahi sus objetos intermedios.
export TMPDIR="$PWD/build/tmp" TMP="$PWD/build/tmp" TEMP="$PWD/build/tmp"

FUENTES=(src/*.cpp src/Chipset/*.cpp src/Peripheral/*.cpp src/Data/*.cpp)

compilar() {
    local nombre="$1" principal="$2"
    "$GPP" -std=c++17 -O2 -Wall -I compat -I src -I include \
        "${FUENTES[@]}" "$principal" -o "build/$nombre" "${@:3}"
}

status=0
compilados=()

# Para iterar rapido sobre un banco sin recompilar cinco veces todo el nucleo:
#   CALCEMU_TEST_ONLY=burst_input_test ./build_test.sh
# Vacio conserva el comportamiento historico y compila la bateria completa.
compilar_seleccionado() {
    local nombre="$1" principal="$2" corto=${1%.exe}
    if [ -n "${CALCEMU_TEST_ONLY:-}" ] && \
       [ "$CALCEMU_TEST_ONLY" != "$nombre" ] && \
       [ "$CALCEMU_TEST_ONLY" != "$corto" ]; then
        return 0
    fi
    if compilar "$nombre" "$principal" "${@:3}"; then
        compilados+=("build/$nombre")
    else
        status=$?
    fi
}

compilar_seleccionado harness.exe test/harness.cpp "$@"
compilar_seleccionado snapshot_test.exe test/snapshot_test.cpp "$@"
compilar_seleccionado power_on_test.exe test/power_on_test.cpp "$@"
compilar_seleccionado test_sequential.exe test/test_sequential.cpp "$@"
compilar_seleccionado burst_input_test.exe test/burst_input_test.cpp "$@"
compilar_seleccionado atomic_tap_api_test.exe test/atomic_tap_api_test.cpp "$@" -pthread

if [ ${#compilados[@]} -eq 0 ] && [ $status -eq 0 ]; then
    echo "CALCEMU_TEST_ONLY no coincide con ningun banco: ${CALCEMU_TEST_ONLY:-<vacio>}" >&2
    exit 2
fi

if [ $status -eq 0 ]; then
    echo "ok -> ${compilados[*]}"
else
    echo "fallo de compilacion ($status)"
fi
exit $status
