# syntax=docker/dockerfile:1
# =============================================================================
# TOMOYO Web Manager — image Docker
#
# IMPORTANT : cette application administre le module de securite noyau
# TOMOYO (LSM) de la MACHINE HOTE via /sys/kernel/security/tomoyo. Un
# conteneur ne peut agir sur cette interface qu'en partageant l'espace de
# noms noyau de l'hote (voir docker/docker-compose.yml : privileged + bind
# mount). Pour un serveur de production, le deploiement natif via systemd
# (voir docs/DEPLOIEMENT.md) reste l'option recommandee car il evite ce
# besoin de conteneur privilegie. Cette image est fournie parce que le
# dossier de conception du projet retient Docker comme cible de
# deploiement ; les deux options sont documentees.
# =============================================================================

# ---- Stage 1: build ----
FROM ubuntu:24.04 AS builder

RUN apt-get update && \
    apt-get install -y --no-install-recommends gcc make && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src/backend
COPY backend/Makefile ./Makefile
COPY backend/src ./src
RUN make clean && make

# ---- Stage 2: runtime ----
FROM ubuntu:24.04

RUN useradd --system --no-create-home --shell /usr/sbin/nologin twm && \
    mkdir -p /app/frontend /app/config /app/data

COPY --from=builder /src/backend/tomoyo-web-manager /app/tomoyo-web-manager
COPY frontend /app/frontend
COPY docker/tomoyo-web-manager.docker.conf /app/config/tomoyo-web-manager.conf

WORKDIR /app
EXPOSE 8088

# The service must be able to write to /sys/kernel/security/tomoyo when
# bind-mounted from the host (real mode); it degrades gracefully to a
# local mock tree otherwise (demo mode). It also needs write access to
# /app/data for session/history/backup bookkeeping.
VOLUME ["/app/data"]

ENTRYPOINT ["/app/tomoyo-web-manager"]
CMD ["/app/config/tomoyo-web-manager.conf"]
