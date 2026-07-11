#!/bin/bash
set -e
cd /home/claude/tomoyo-web-manager/backend
rm -rf data
nohup ./tomoyo-web-manager config/tomoyo-web-manager.conf > /tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 1

BASE=http://127.0.0.1:8088
JAR=/tmp/twm_cookies.txt
rm -f "$JAR"

echo "### 1. GET /api/session (not authenticated)"
curl -s "$BASE/api/session"; echo

echo "### 2. Login with wrong password"
curl -s -X POST "$BASE/api/login" -d '{"login":"admin","password":"bad"}'; echo

echo "### 3. Login OK (save cookie jar)"
curl -s -c "$JAR" -X POST "$BASE/api/login" -d '{"login":"admin","password":"changeme"}'; echo

echo "### 4. GET /api/session (authenticated)"
curl -s -b "$JAR" "$BASE/api/session"; echo

echo "### 5. GET /api/domains"
curl -s -b "$JAR" "$BASE/api/domains"; echo

echo "### 6. GET /api/rules?domain=<kernel> /usr/sbin/sshd"
curl -s -b "$JAR" -G "$BASE/api/rules" --data-urlencode 'domain=<kernel> /usr/sbin/sshd'; echo

echo "### 7. POST /api/mode -> switch sshd domain to enforcing"
curl -s -b "$JAR" -X POST "$BASE/api/mode" \
  -d '{"domain":"<kernel> /usr/sbin/sshd","mode":"enforcing"}'; echo

echo "### 8. Re-check domains (mode should now be enforcing)"
curl -s -b "$JAR" "$BASE/api/domains"; echo

echo "### 9. POST /api/rules -> add a VALID rule"
curl -s -b "$JAR" -X POST "$BASE/api/rules" \
  -d '{"domain":"<kernel> /usr/sbin/sshd","action":"add","rule":"file read /etc/hosts"}'; echo

echo "### 10. POST /api/rules -> add an INVALID rule (bad syntax)"
curl -s -b "$JAR" -X POST "$BASE/api/rules" \
  -d '{"domain":"<kernel> /usr/sbin/sshd","action":"add","rule":"frobnicate everything"}'; echo

echo "### 11. Re-check rules for sshd domain (should include new rule)"
curl -s -b "$JAR" -G "$BASE/api/rules" --data-urlencode 'domain=<kernel> /usr/sbin/sshd'; echo

echo "### 12. POST /api/rollback (undo the rule add)"
curl -s -b "$JAR" -X POST "$BASE/api/rollback"; echo

echo "### 13. Re-check rules after rollback (new rule should be gone)"
curl -s -b "$JAR" -G "$BASE/api/rules" --data-urlencode 'domain=<kernel> /usr/sbin/sshd'; echo

echo "### 14. GET /api/logs (all)"
curl -s -b "$JAR" "$BASE/api/logs"; echo

echo "### 15. GET /api/logs?violations=1"
curl -s -b "$JAR" -G "$BASE/api/logs" --data-urlencode 'violations=1'; echo

echo "### 16. GET /api/stat"
curl -s -b "$JAR" "$BASE/api/stat"; echo

echo "### 17. GET /api/history"
curl -s -b "$JAR" "$BASE/api/history"; echo

echo "### 18. GET /api/export?domain=<kernel> /usr/sbin/sshd"
curl -s -b "$JAR" -G "$BASE/api/export" --data-urlencode 'domain=<kernel> /usr/sbin/sshd'; echo

echo "### 19. POST /api/generate with exec not in allow-list (expect 403)"
curl -s -b "$JAR" -X POST "$BASE/api/generate" -d '{"exec_path":"/bin/ls"}'; echo

echo "### 20. Static file serving: GET /"
curl -s -o /dev/null -w "HTTP %{http_code} - %{content_type}\n" "$BASE/"

echo "### 21. Logout"
curl -s -b "$JAR" -X POST "$BASE/api/logout"; echo

echo "### 22. GET /api/domains after logout (expect 401)"
curl -s -b "$JAR" "$BASE/api/domains"; echo

echo "### 23. Access without any session cookie (expect 401)"
curl -s "$BASE/api/domains"; echo

kill $SERVER_PID 2>/dev/null || true
echo "=== DONE ==="
