#!/bin/bash
# =============================================================================
# TOMOYO Web Manager — installation native sur Ubuntu (systemd)
#
# A executer depuis la racine du depot, en tant que root (ou via sudo) :
#   sudo bash scripts/install_ubuntu.sh
#
# Ce script :
#   1. installe les dependances de compilation (gcc, make)
#   2. compile le backend C
#   3. installe le binaire + le frontend dans /opt/tomoyo-web-manager
#   4. installe la configuration dans /etc/tomoyo-web-manager (si absente)
#   5. cree le repertoire de donnees /var/lib/tomoyo-web-manager
#   6. installe et active le service systemd
#
# Il NE modifie PAS la configuration GRUB pour activer le module noyau
# TOMOYO : cette etape, potentiellement plus sensible (necessite un
# redemarrage), est decrite separement dans docs/DEPLOIEMENT.md.
# =============================================================================
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Ce script doit etre execute en tant que root (sudo bash $0)." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_DIR=/opt/tomoyo-web-manager
CONFIG_DIR=/etc/tomoyo-web-manager
DATA_DIR=/var/lib/tomoyo-web-manager
SERVICE_NAME=tomoyo-web-manager

echo "==> [1/6] Installation des dependances de compilation (gcc, make)..."
apt-get update -qq
apt-get install -y --no-install-recommends gcc make

echo "==> [2/6] Compilation du backend..."
make -C "$SCRIPT_DIR/backend" clean
make -C "$SCRIPT_DIR/backend"

echo "==> [3/6] Installation du binaire et du frontend dans $INSTALL_DIR..."
mkdir -p "$INSTALL_DIR"
install -m 0755 "$SCRIPT_DIR/backend/tomoyo-web-manager" "$INSTALL_DIR/tomoyo-web-manager"
rm -rf "$INSTALL_DIR/frontend"
cp -r "$SCRIPT_DIR/frontend" "$INSTALL_DIR/frontend"

echo "==> [4/6] Installation de la configuration dans $CONFIG_DIR..."
mkdir -p "$CONFIG_DIR"
if [ -f "$CONFIG_DIR/tomoyo-web-manager.conf" ]; then
    echo "    Configuration existante conservee (non ecrasee)."
else
    cp "$SCRIPT_DIR/backend/config/tomoyo-web-manager.conf.example" \
       "$CONFIG_DIR/tomoyo-web-manager.conf"
    echo "    Configuration par defaut installee : $CONFIG_DIR/tomoyo-web-manager.conf"
    echo "    /!\\ Le mot de passe par defaut est 'changeme' - a changer immediatement"
    echo "        avec : sudo bash scripts/set_admin_password.sh $CONFIG_DIR/tomoyo-web-manager.conf"
fi

echo "==> [5/6] Creation du repertoire de donnees $DATA_DIR..."
mkdir -p "$DATA_DIR"
chmod 0750 "$DATA_DIR"

echo "==> [6/6] Installation du service systemd..."
cp "$SCRIPT_DIR/systemd/tomoyo-web-manager.service" \
   "/etc/systemd/system/$SERVICE_NAME.service"
systemctl daemon-reload
systemctl enable "$SERVICE_NAME"
systemctl restart "$SERVICE_NAME"

sleep 1
echo
echo "======================================================================="
if systemctl is-active --quiet "$SERVICE_NAME"; then
    echo "Service demarre avec succes."
else
    echo "/!\\ Le service ne semble pas actif. Diagnostic :"
    echo "    sudo systemctl status $SERVICE_NAME"
    echo "    sudo journalctl -u $SERVICE_NAME -n 50"
fi
echo "======================================================================="
echo
echo "Prochaines etapes recommandees :"
echo "  1. Changer le mot de passe administrateur :"
echo "       sudo bash scripts/set_admin_password.sh $CONFIG_DIR/tomoyo-web-manager.conf"
echo "  2. Verifier si TOMOYO est actif au niveau noyau :"
echo "       cat /sys/kernel/security/lsm"
echo "     (voir docs/DEPLOIEMENT.md si 'tomoyo' n'y figure pas)"
echo "  3. Configurer un reverse proxy HTTPS (nginx + certbot) :"
echo "       voir docs/DEPLOIEMENT.md, section 'Exposition HTTPS'"
echo "  4. Acceder a l'application :"
echo "       http://127.0.0.1:8088  (ou via le nom de domaine une fois nginx configure)"
echo
