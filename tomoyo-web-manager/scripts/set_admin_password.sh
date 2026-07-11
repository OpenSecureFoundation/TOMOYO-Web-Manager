#!/bin/bash
# Computes the SHA-256 hash of a new admin password and writes it into
# a TOMOYO Web Manager config file (admin_pass_sha256=...).
#
# Usage:
#   scripts/set_admin_password.sh /etc/tomoyo-web-manager/tomoyo-web-manager.conf
#
set -euo pipefail

CONFIG_FILE="${1:-}"

if [ -z "$CONFIG_FILE" ]; then
    echo "Usage: $0 <chemin-vers-tomoyo-web-manager.conf>" >&2
    exit 1
fi
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Fichier de configuration introuvable : $CONFIG_FILE" >&2
    exit 1
fi

echo -n "Nouveau mot de passe administrateur : "
read -rs PASSWORD
echo
echo -n "Confirmez le mot de passe            : "
read -rs PASSWORD_CONFIRM
echo

if [ "$PASSWORD" != "$PASSWORD_CONFIRM" ]; then
    echo "Erreur : les deux saisies ne correspondent pas." >&2
    exit 1
fi
if [ -z "$PASSWORD" ]; then
    echo "Erreur : mot de passe vide refuse." >&2
    exit 1
fi

HASH=$(printf '%s' "$PASSWORD" | sha256sum | awk '{print $1}')

if grep -q '^admin_pass_sha256=' "$CONFIG_FILE"; then
    sed -i "s/^admin_pass_sha256=.*/admin_pass_sha256=${HASH}/" "$CONFIG_FILE"
else
    echo "admin_pass_sha256=${HASH}" >> "$CONFIG_FILE"
fi

echo "OK : admin_pass_sha256 mis a jour dans $CONFIG_FILE"
echo "Redemarrez le service pour appliquer le changement :"
echo "  sudo systemctl restart tomoyo-web-manager"
