# TOMOYO Web Manager

Plateforme web de gestion et de visualisation des politiques **TOMOYO
Linux** (module de sécurité noyau à contrôle d'accès obligatoire basé
sur les chemins). Développée à partir du *Cahier d'analyse* et du
*Dossier de conception* du projet TOMOYO-Web-Manager.

- **Backend** : C (serveur HTTP natif, sans framework ni dépendance
  externe hors la bibliothèque standard et `pthread`)
- **Frontend** : HTML / CSS / JavaScript (aucun framework)
- **Déploiement** : natif via `systemd` (recommandé) ou Docker

➡️ **Procédure de déploiement complète : [`docs/DEPLOIEMENT.md`](docs/DEPLOIEMENT.md)**

---

## Fonctionnalités (cahier d'analyse)

| Cas d'utilisation | Endpoint API | Vue frontend |
|---|---|---|
| S'authentifier | `POST /api/login` | Page de connexion |
| Visualiser policies | `GET /api/domains`, `GET /api/rules` | Politiques |
| Basculer mode (Learning/Enforcement) | `POST /api/mode` | Politiques (modale de confirmation) |
| Éditer règle (validation + rollback) | `POST /api/rules`, `POST /api/rollback` | Éditeur de règles |
| Génération automatique (capture d'apprentissage) | `POST /api/generate`, `GET /api/export` | Éditeur de règles |
| Consulter logs | `GET /api/logs` | Journaux |
| Historique des modifications | `GET /api/history` | Historique |

Module transversal : authentification par session, HTTPS (via reverse
proxy en production), journalisation systématique de toute action
sensible.

## Démarrage rapide (développement local)

```bash
make -C backend
cd backend
./tomoyo-web-manager config/tomoyo-web-manager.conf
```

Ouvrez `http://127.0.0.1:8088/` — identifiant `admin`, mot de passe
`changeme`.

Aucun noyau TOMOYO n'est requis pour développer : en son absence,
l'application bascule automatiquement en **mode démo (mock)**, avec un
jeu de données TOMOYO simulé mais réaliste (domaines, règles, logs),
pleinement fonctionnel pour tester ou présenter l'interface.

## Tests

```bash
bash backend/test_api.sh        # suite de tests API (23 scénarios)
bash scripts/demo_scenario.sh   # scénario de démonstration end-to-end
```

## Architecture

```
Navigateur (HTML/CSS/JS)
        │  fetch() JSON + cookie de session
        ▼
Serveur HTTP en C (thread par connexion)
   ├── auth.c        sessions, hachage SHA-256 du mot de passe
   ├── routes.c       endpoints REST, fichiers statiques
   ├── tomoyo_iface.c interface noyau (lecture/écriture policy),
   │                   simulateur "mock" fidèle en l'absence de noyau
   ├── json.c         (dé)sérialisation JSON minimale, sans dépendance
   └── http_server.c   serveur HTTP/1.1 bas niveau (sockets POSIX)
        │
        ▼
/sys/kernel/security/tomoyo/   (domain_policy, profile, stat, audit)
```

Voir [`docs/DEPLOIEMENT.md`](docs/DEPLOIEMENT.md) pour la procédure
d'installation détaillée (native et Docker), l'activation du module
noyau TOMOYO, la mise en HTTPS, et le dépannage.

## Structure du dépôt

```
backend/    serveur C : src/, Makefile, config/
frontend/   dashboard HTML/CSS/JS
docker/     Dockerfile, docker-compose.yml
systemd/    unité de service pour le déploiement natif
scripts/    installation Ubuntu, changement de mot de passe, démo
docs/       guide de déploiement, config nginx d'exemple
```

## Sécurité

- Mot de passe administrateur haché (SHA-256), jamais stocké en clair.
- Sessions par cookie `HttpOnly; SameSite=Strict` (+ `Secure` derrière
  HTTPS).
- Toute écriture de politique est précédée d'une sauvegarde
  (`domain_policy.bak`) permettant un retour arrière immédiat.
- Toute règle est validée syntaxiquement côté serveur avant application.
- La capture d'apprentissage automatique n'exécute que des chemins
  explicitement listés en configuration (`allow_exec`).
- Historique complet des actions sensibles (`/api/history`).

Ce projet est un travail académique de démonstration ; voir
`docs/DEPLOIEMENT.md` §9 pour le détail des mesures de sécurité et
leurs limites connues avant tout usage en production réelle.
