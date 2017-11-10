#!/bin/bash

x11_server=Xwayland
xmir_installed=$(which Xmir | wc -l)
if [ "${xmir_installed}" != "0" ]
then
    x11_server=Xmir
fi

while [ $# -gt 0 ]
do
  if [ "$1" == "--help" -o "$1" == "-h" ]
  then
    echo "$(basename $0) - Handy launch script for providing an X11 server"
    echo "Usage: $(basename $0) [options] command"
    echo "Options are:"
    echo "    -Xmir      use Xmir"
    echo "    -Xwayland  use Xwayland"
    echo "(default is -${x11_server})"
    exit 0
  elif [ "$1" == "-Xmir" ];     then x11_server=Xmir
  elif [ "$1" == "-Xwayland" ]; then x11_server=Xwayland
  elif [ "${1:0:1}" == "-" ];   then echo "Unknown option: $1"; exit 1
  else break
  fi
  shift
done

unset QT_QPA_PLATFORMTHEME
export XDG_SESSION_TYPE=x11
export GDK_BACKEND=x11
export QT_QPA_PLATFORM=xcb
export SDL_VIDEODRIVER=x11

x_server_installed=$(which ${x11_server} | wc -l)
if [ "${x_server_installed}" == "0" ]
then
    echo "Error: Need ${x11_server}"
    echo "On Ubuntu run \"sudo apt install xmir xwayland\""; 
    echo "On Fedora run \"sudo dnf install xorg-x11-server-Xwayland\""; 
    exit 1
fi

if [ "${x11_server}" == "Xmir" ];
then
  if   [ -e "${XDG_RUNTIME_DIR}/miral_socket" ];
  then
    socket_value=${XDG_RUNTIME_DIR}/miral_socket
  elif [ -e "${XDG_RUNTIME_DIR}/mir_socket" ];
  then
    socket_value=${XDG_RUNTIME_DIR}/mir_socket
  else
    echo "Error: Cannot detect Mir endpoint"; exit 1
  fi
  x11_server_args=-rootless
elif [ "${x11_server}" == "Xwayland" ];
then
  if [ -e "${XDG_RUNTIME_DIR}/miral_wayland" ];
  then
    socket_value=miral_wayland
  elif [ -e "${XDG_RUNTIME_DIR}/wayland-1" ]
  then
    socket_value=wayland-1
  elif [ -e "${XDG_RUNTIME_DIR}/wayland-0" ]
  then
    socket_value=wayland-0
  else
    echo "Error: Cannot detect Mir-Wayland endpoint"; exit 1
  fi
  x11_server_args=
fi

port=0

while [ -e "/tmp/.X11-unix/X${port}" ]; do
    let port+=1
done

MIR_SOCKET=${socket_value} WAYLAND_DISPLAY=${socket_value} ${x11_server} ${x11_server_args} :${port} & pid=$!
while [ ! -e "/tmp/.X11-unix/X${port}" ]; do echo "waiting for DISPLAY=:${port}"; sleep 1 ;done
DISPLAY=:${port} "$@"
kill ${pid}
