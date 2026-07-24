# Déploiement de TOMOYO Web Manager sur Ubuntu

Ce document décrit la procédure complète de déploiement de la plateforme,
conformément au périmètre défini dans le *Cahier d'analyse* et le
*Dossier de conception* : authentification, visualisation/édition des
politiques TOMOYO, bascule Learning/Enforcement, consultation des
journaux, communication chiffrée (HTTPS) et journalisation (audit).

Deux modes de déploiement sont documentés :

- **Option A — Déploiement natif (systemd)** : recommandé pour un
  serveur de production, car l'application a besoin d'écrire
  directement dans l'interface noyau `/sys/kernel/security/tomoyo`.
- **Option B — Déploiement Docker** : retenu par le *Dossier de
  conception*. Fonctionnel, mais nécessite un conteneur privilégié
  (voir limitations, §4.3).

Si le noyau de la machine ne tourne pas avec TOMOYO activé (ou hors
Linux), l'application **détecte automatiquement** l'absence de
`/sys/kernel/security/tomoyo` et bascule en **mode démo (mock)** :
entièrement fonctionnelle pour une démonstration, mais sans effet sur
un vrai noyau. Utile pour développer, tester ou soutenir le projet sans
dépendre d'un noyau TOMOYO.

---

## 0. Prérequis

- Ubuntu 22.04 LTS ou 24.04 LTS (recommandé), accès `sudo`/root.
- Pour un pilotage **réel** de TOMOYO : un noyau Linux compilé avec
  `CONFIG_SECURITY_TOMOYO=y` (c'est le cas du noyau générique Ubuntu)
  et la possibilité de redémarrer la machine pour activer le module.
- Ports 80/443 ouverts si vous exposez le service publiquement en HTTPS.
- Aucune dépendance externe pour compiler le backend au-delà de `gcc`,
  `make` et `pthread` (bibliothèque standard) : pas de base de
  données, pas de framework web, pas d'OpenSSL requis (le hachage
  SHA-256 du mot de passe est implémenté en interne).

---

## 1. Récupération du projet

```bash
# Copiez l'archive du projet sur le serveur, puis :
cd tomoyo-web-manager
```

L'arborescence livrée :

```
tomoyo-web-manager/
├── backend/            # serveur HTTP en C (API + logique TOMOYO)
├── frontend/            # HTML / CSS / JS (dashboard)
├── docker/               # Dockerfile, docker-compose.yml
├── systemd/              # unité systemd pour le déploiement natif
├── scripts/              # installation, mot de passe, scénario de démo
└── docs/                 # ce document, config nginx d'exemple
```

---

## 2. Option A — Déploiement natif (recommandé)

### 2.1 Installation automatique

```bash
sudo bash scripts/install_ubuntu.sh
```

Ce script installe `gcc`/`make`, compile le backend, installe le
binaire et le frontend dans `/opt/tomoyo-web-manager`, la configuration
dans `/etc/tomoyo-web-manager/`, crée `/var/lib/tomoyo-web-manager`
pour les données applicatives, puis installe et démarre le service
systemd. Suivez les instructions affichées en fin d'exécution
(changement du mot de passe, etc.).

Passez directement à la **section 2.3** une fois le script exécuté.

### 2.2 Installation manuelle (pas à pas)

Si vous préférez comprendre/maîtriser chaque étape :

```bash
# 1. Dépendances de compilation
sudo apt update
sudo apt install -y gcc make

# 2. Compilation
make -C backend

# 3. Installation des binaires et du frontend
sudo mkdir -p /opt/tomoyo-web-manager
sudo install -m 0755 backend/tomoyo-web-manager /opt/tomoyo-web-manager/
sudo cp -r frontend /opt/tomoyo-web-manager/frontend

# 4. Configuration
sudo mkdir -p /etc/tomoyo-web-manager
sudo cp backend/config/tomoyo-web-manager.conf.example \
        /etc/tomoyo-web-manager/tomoyo-web-manager.conf

# 5. Répertoire de données (sauvegardes de policy, historique)
sudo mkdir -p /var/lib/tomoyo-web-manager
sudo chmod 0750 /var/lib/tomoyo-web-manager

# 6. Service systemd
sudo cp systemd/tomoyo-web-manager.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now tomoyo-web-manager
```

### 2.3 Changer le mot de passe administrateur

**Indispensable avant toute mise en production** — la valeur livrée par
défaut (`changeme`) ne doit jamais rester active :

```bash
sudo bash scripts/set_admin_password.sh /etc/tomoyo-web-manager/tomoyo-web-manager.conf
sudo systemctl restart tomoyo-web-manager
```

### 2.4 Activer réellement le module noyau TOMOYO

Par défaut, Ubuntu démarre avec AppArmor comme module LSM ; TOMOYO est
présent dans le noyau mais inactif. Sans cette étape, l'application
tourne en **mode démo (mock)** — utile pour tester l'interface, mais
sans effet sur le système réel.

1. Vérifiez l'état actuel :

   ```bash
   cat /sys/kernel/security/lsm
   ```

   Si `tomoyo` n'apparaît pas dans la liste, il n'est pas actif.

2. Installez les outils TOMOYO userspace :

   ```bash
   sudo apt install -y tomoyo-tools
   ```

3. Éditez `/etc/default/grub` et ajoutez `tomoyo` à la liste `lsm=`
   (en conservant les modules existants, ordre important — `tomoyo`
   idéalement en premier des modules "majeurs") :

   ```bash
   # exemple : ajoute tomoyo à cote d'apparmor sans le retirer
   GRUB_CMDLINE_LINUX_DEFAULT="quiet splash lsm=lockdown,capability,yama,apparmor,bpf,tomoyo"
   ```

   Vous pouvez aussi remplacer entièrement `apparmor` par `tomoyo` dans
   cette liste si vous ne souhaitez utiliser que TOMOYO comme MAC.

4. Appliquez et redémarrez :

   ```bash
   sudo update-grub
   sudo reboot
   ```

5. Après redémarrage, vérifiez à nouveau :

   ```bash
   cat /sys/kernel/security/lsm      # doit contenir "tomoyo"
   ls /sys/kernel/security/tomoyo/   # doit lister domain_policy, profile, stat...
   ```

6. Redémarrez le service applicatif pour qu'il détecte le noyau réel :

   ```bash
   sudo systemctl restart tomoyo-web-manager
   sudo journalctl -u tomoyo-web-manager -n 20
   ```

   Le journal doit afficher `TOMOYO securityfs detected ... (real mode)`
   au lieu de l'avertissement de mode démo.

> Au premier démarrage avec TOMOYO actif, tous les domaines sont en
> général en mode **Learning** ou **Disabled** — c'est normal : voir le
> module "Génération automatique" de l'application pour construire une
> politique par apprentissage avant de basculer en **Enforcing**.

### 2.5 Autoriser des exécutables pour la capture d'apprentissage

Le module « Génération automatique » (capture d'apprentissage) ne peut
lancer que des chemins explicitement autorisés, pour éviter qu'un appel
API ne puisse exécuter un programme arbitraire sur le serveur :

```bash
sudo tee -a /etc/tomoyo-web-manager/tomoyo-web-manager.conf > /dev/null << 'EOF'
allow_exec=/usr/bin/monapplication
EOF
sudo systemctl restart tomoyo-web-manager
```

### 2.6 Gestion du service

```bash
sudo systemctl status  tomoyo-web-manager
sudo systemctl stop    tomoyo-web-manager
sudo systemctl start   tomoyo-web-manager
sudo systemctl restart tomoyo-web-manager
sudo journalctl -u tomoyo-web-manager -f      # logs en direct
```

### 2.7 Exécution en utilisateur dédié (option avancée, moindre privilège)

Le service est configuré par défaut pour tourner en `root`, car TOMOYO
autorise nativement l'UID 0 à écrire sa politique. Pour éviter root :

1. Enregistrez le chemin absolu du binaire comme *manager* autorisé :

   ```bash
   echo "/opt/tomoyo-web-manager/tomoyo-web-manager" | \
     sudo tee -a /sys/kernel/security/tomoyo/manager
   ```

2. Activez `manage_by_non_root` dans le profil TOMOYO concerné (voir
   `tomoyo-setprofile`/`tomoyo-editpolicy` fournis par `tomoyo-tools`).
3. Créez un utilisateur dédié et ajustez `User=`/`Group=` dans
   `/etc/systemd/system/tomoyo-web-manager.service`, puis
   `sudo systemctl daemon-reload && sudo systemctl restart tomoyo-web-manager`.

Cette étape est optionnelle : le comportement par défaut (root) reste
le plus simple et le plus robuste pour un projet de ce type.

---

## 3. Option B — Déploiement Docker

Le *Dossier de conception* retient Docker comme cible de déploiement ;
cette option est donc fournie et fonctionnelle.

```bash
cd tomoyo-web-manager
docker compose -f docker/docker-compose.yml up -d --build
```

Par défaut, le conteneur tourne **en mode démo (mock)** : le
bind-mount vers le vrai `/sys/kernel/security/tomoyo` de l'hôte est
volontairement commenté dans `docker/docker-compose.yml` (à décommenter
uniquement sur un hôte où TOMOYO est actif, §2.4).

Consultez les journaux et l'état :

```bash
docker compose -f docker/docker-compose.yml logs -f
docker compose -f docker/docker-compose.yml ps
```

### 3.1 Limitations à connaître

- Pour piloter le **vrai** module noyau, le conteneur doit tourner en
  `privileged: true` (déjà configuré) et monter
  `/sys/kernel/security/tomoyo` de l'hôte — ce qui réduit
  significativement l'isolation habituelle de Docker : le conteneur
  obtient de facto un accès étendu au noyau hôte.
- Pour un serveur de production dédié à cet usage, le déploiement
  **natif (Option A)** reste préférable : il évite ce compromis
  puisqu'il n'y a pas d'espace de noms à traverser.
- Le mot de passe par défaut (`changeme`) doit être changé avant toute
  exposition publique — éditez `docker/tomoyo-web-manager.docker.conf`
  (ou montez votre propre fichier de configuration) puis reconstruisez
  l'image.

---

## 4. Exposition HTTPS (communication chiffrée)

L'application écoute en HTTP simple sur `127.0.0.1:8088` (interne
uniquement). Le *Cahier d'analyse* exige une communication chiffrée :
on termine le HTTPS avec **nginx + Let's Encrypt (certbot)** devant le
service.

```bash
sudo apt install -y nginx certbot python3-certbot-nginx

sudo cp docs/nginx-tomoyo-web-manager.conf /etc/nginx/sites-available/tomoyo-web-manager
sudo ln -s /etc/nginx/sites-available/tomoyo-web-manager /etc/nginx/sites-enabled/
sudo sed -i 's/votre-domaine.example/VOTRE_DOMAINE_REEL/' \
  /etc/nginx/sites-available/tomoyo-web-manager

sudo nginx -t
sudo systemctl reload nginx

sudo certbot --nginx -d VOTRE_DOMAINE_REEL
```

Certbot modifie automatiquement le fichier nginx pour ajouter le
certificat et la redirection HTTP → HTTPS.

Ensuite, activez l'attribut `Secure` du cookie de session :

```bash
sudo sed -i 's/^cookie_secure=0/cookie_secure=1/' \
  /etc/tomoyo-web-manager/tomoyo-web-manager.conf
sudo systemctl restart tomoyo-web-manager
```

---

## 5. Pare-feu (ufw)

```bash
sudo ufw allow OpenSSH
sudo ufw allow 'Nginx Full'   # 80 + 443
sudo ufw enable
```

Le port applicatif `8088` ne doit **pas** être exposé publiquement (il
n'écoute d'ailleurs que sur `127.0.0.1` par défaut) ; seul nginx doit
être joignable depuis l'extérieur.

---

## 6. Vérification post-déploiement

```bash
# Sante du service
curl -s http://127.0.0.1:8088/api/session

# Scenario de demonstration complet (cf. cahier d'analyse : "execute
# le scenario de demonstration")
bash scripts/demo_scenario.sh http://127.0.0.1:8088 admin 'VotreNouveauMotDePasse'
```

Depuis un navigateur : `https://votre-domaine.example/` (ou
`http://127.0.0.1:8088/` en local) doit afficher la page de connexion.

---

## 7. Sauvegardes et mise à jour

- **Sauvegarde** : le répertoire `/var/lib/tomoyo-web-manager` contient
  l'historique des modifications (`mode_history.log`) et la dernière
  sauvegarde de policy (`domain_policy.bak`). Sauvegardez-le
  périodiquement si vous souhaitez conserver l'historique au-delà de
  la politique noyau elle-même.
- **Mise à jour** : recompilez (`make -C backend`), réinstallez le
  binaire et le frontend (étapes 2.2.3), puis
  `sudo systemctl restart tomoyo-web-manager`. La configuration et les
  données existantes ne sont pas affectées.

---

## 8. Dépannage

| Symptôme | Cause probable | Action |
|---|---|---|
| Le service loggue `MODE DEMO/MOCK ACTIF` | TOMOYO non actif au niveau noyau | Voir §2.4 |
| `bind() failed` au démarrage | Port déjà utilisé | `sudo ss -tlnp \| grep 8088` |
| 401 permanent malgré bon mot de passe | Hash non régénéré après édition manuelle du `.conf` | Utilisez `scripts/set_admin_password.sh` |
| Écriture de policy sans effet visible | Processus non autorisé par TOMOYO (`manager`) | Vérifiez que le service tourne en `root`, ou voir §2.7 |
| Page blanche / 404 sur `/` | `static_root` incorrect dans la config | Vérifiez le chemin dans le `.conf` actif |

Journal détaillé : `sudo journalctl -u tomoyo-web-manager -n 100 --no-pager`

---

## 9. Récapitulatif sécurité (module transversal du cahier d'analyse)

| Exigence du cahier | Implémentation |
|---|---|
| Authentification | Session par cookie `HttpOnly; SameSite=Strict`, mot de passe haché SHA-256, compte admin unique |
| Communication chiffrée (HTTPS) | Terminaison TLS via nginx + certbot (§4) ; cookie `Secure` en production |
| Journalisation (audit) | Journal noyau TOMOYO (`/api/logs`) + historique applicatif des changements (`/api/history`, `mode_history.log`) |
| Validation avant application | Vérification de syntaxe côté serveur (`file/network/misc/capability/ipc/task`) avant toute écriture de policy |
| Prévisualisation et rollback | Sauvegarde automatique de `domain_policy` avant chaque modification ; endpoint de retour arrière |
| Confirmation avant bascule de mode | Modale de confirmation côté interface avant tout changement Learning/Enforcement |
