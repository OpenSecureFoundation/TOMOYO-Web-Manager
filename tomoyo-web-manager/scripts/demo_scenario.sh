#!/bin/bash
# =============================================================================
# TOMOYO Web Manager — scenario de demonstration
#
# Rejoue de bout en bout les cas d'utilisation du cahier d'analyse :
# authentification, visualisation des policies, bascule de mode,
# edition de regle (avec erreur de syntaxe volontaire puis correction),
# consultation des logs, et retour arriere.
#
# Usage :
#   bash scripts/demo_scenario.sh [base_url] [login] [password]
# Par defaut : http://127.0.0.1:8088 / admin / changeme
# =============================================================================
set -uo pipefail

BASE="${1:-http://127.0.0.1:8088}"
LOGIN="${2:-admin}"
PASSWORD="${3:-changeme}"
JAR="$(mktemp)"

step() { echo; echo "=== $1 ==="; }
pause() { read -rp "   [entree pour continuer] " _ || true; }

step "1. Authentification (S'authentifier)"
curl -s -c "$JAR" -X POST "$BASE/api/login" \
  -d "{\"login\":\"$LOGIN\",\"password\":\"$PASSWORD\"}" | python3 -m json.tool
pause

step "2. Visualisation des policies (Visualiser policies)"
DOMAINS_JSON=$(curl -s -b "$JAR" "$BASE/api/domains")
echo "$DOMAINS_JSON" | python3 -m json.tool
FIRST_DOMAIN=$(echo "$DOMAINS_JSON" | python3 -c "import json,sys; d=json.load(sys.stdin)['domains']; print(d[1]['name'] if len(d)>1 else d[0]['name'])")
echo "   -> domaine choisi pour la suite : $FIRST_DOMAIN"
pause

step "3. Bascule de mode (Basculer mode) -> learning"
curl -s -b "$JAR" -X POST "$BASE/api/mode" \
  -d "{\"domain\":$(python3 -c "import json;print(json.dumps('$FIRST_DOMAIN'))"),\"mode\":\"learning\"}" | python3 -m json.tool
pause

step "4. Edition de regle - tentative invalide (Editer regle / erreur de syntaxe)"
curl -s -b "$JAR" -X POST "$BASE/api/rules" \
  -d "{\"domain\":$(python3 -c "import json;print(json.dumps('$FIRST_DOMAIN'))"),\"action\":\"add\",\"rule\":\"ceci nest pas une regle\"}" | python3 -m json.tool
pause

step "5. Edition de regle - version corrigee, appliquee"
curl -s -b "$JAR" -X POST "$BASE/api/rules" \
  -d "{\"domain\":$(python3 -c "import json;print(json.dumps('$FIRST_DOMAIN'))"),\"action\":\"add\",\"rule\":\"file read /etc/hosts\"}" | python3 -m json.tool
pause

step "6. Consultation des logs (Consulter logs) - violations uniquement"
curl -s -b "$JAR" -G "$BASE/api/logs" --data-urlencode 'violations=1' | python3 -m json.tool
pause

step "7. Retour arriere (rollback de la regle ajoutee a l'etape 5)"
curl -s -b "$JAR" -X POST "$BASE/api/rollback" | python3 -m json.tool
pause

step "8. Historique des modifications"
curl -s -b "$JAR" "$BASE/api/history" | python3 -m json.tool

step "9. Deconnexion"
curl -s -b "$JAR" -X POST "$BASE/api/logout" | python3 -m json.tool

rm -f "$JAR"
echo
echo "=== Scenario termine ==="
