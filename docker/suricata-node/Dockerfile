# PHIMANTEC C11 Suricata node with integrated SuricataMonitor Web UI.
# Ubuntu 24.04 profile with fail-fast APT and OISF Suricata 8 package source.

ARG UBUNTU_BASE=ubuntu:24.04

FROM ${UBUNTU_BASE} AS builder
ARG PHIMANTEC_APT_MIRROR=""
ARG PHIMANTEC_APT_SECURITY_MIRROR=""
ARG PHIMANTEC_APT_RETRIES="0"
ARG PHIMANTEC_APT_TIMEOUT="12"
ARG PHIMANTEC_APT_FORCE_IPV4="1"
ENV DEBIAN_FRONTEND=noninteractive
COPY docker/common/apt_install.sh /usr/local/bin/phimantec_apt_install
RUN /usr/local/bin/phimantec_apt_install \
      build-essential cmake pkg-config libssl-dev ca-certificates
WORKDIR /src
COPY CMakeLists.txt ./
COPY shared ./shared
COPY appliance ./appliance
COPY suricata-node ./suricata-node
COPY tools ./tools
COPY tests ./tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF \
 && cmake --build build -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"

FROM ${UBUNTU_BASE} AS runtime
ARG PHIMANTEC_APT_MIRROR=""
ARG PHIMANTEC_APT_SECURITY_MIRROR=""
ARG PHIMANTEC_APT_RETRIES="0"
ARG PHIMANTEC_APT_TIMEOUT="12"
ARG PHIMANTEC_APT_FORCE_IPV4="1"
ARG PHIMANTEC_SURICATA_APT_PPA="ppa:oisf/suricata-stable"
ENV DEBIAN_FRONTEND=noninteractive \
    SURIMON_CONFIG=/etc/phimantec/suricata-monitor.yaml \
    PHIMANTEC_NODE_CONFIG=/etc/phimantec/phimantec-suricata-node.conf
COPY docker/common/apt_install.sh /usr/local/bin/phimantec_apt_install
RUN /usr/local/bin/phimantec_apt_install \
      ca-certificates gnupg software-properties-common python3-launchpadlib \
 && if [ -n "${PHIMANTEC_SURICATA_APT_PPA}" ]; then add-apt-repository -y "${PHIMANTEC_SURICATA_APT_PPA}"; fi
RUN /usr/local/bin/phimantec_apt_install \
      ca-certificates iproute2 nftables iptables python3 python3-flask python3-yaml \
      suricata tini \
 && { command -v suricata-update >/dev/null 2>&1 || /usr/local/bin/phimantec_apt_install suricata-update; } \
 && command -v suricata-update >/dev/null 2>&1 \
 && suricata --build-info | tee /tmp/phimantec-suricata-build-info.txt \
 && grep -Eq 'This is Suricata version ([8-9]|[1-9][0-9])\.' /tmp/phimantec-suricata-build-info.txt \
 && groupadd --system phimantec \
 && useradd --system --no-create-home --shell /usr/sbin/nologin --gid phimantec phimantec \
 && { usermod -aG adm phimantec || true; } \
 && install -d -o root -g phimantec -m 0750 /etc/phimantec \
 && install -d -o phimantec -g phimantec -m 0750 /var/log/phimantec \
 && install -d -o phimantec -g phimantec -m 0750 /var/lib/suricata-monitor \
 && install -d -o root -g root -m 0755 /etc/suricata/rules \
 && install -d -o root -g adm -m 0755 /var/log/suricata \
 && install -d -o root -g root -m 0755 /var/run/suricata \
 && rm -f /tmp/phimantec-suricata-build-info.txt
COPY --from=builder /src/build/phimantec-lane-sink /usr/local/bin/phimantec-lane-sink
COPY --from=builder /src/build/phimantec-inspectord /usr/local/bin/phimantec-inspectord
COPY --from=builder /src/build/phimantec-flowbus-probe /usr/local/bin/phimantec-flowbus-probe
COPY config/docker/phimantec-suricata-node.conf /etc/phimantec/phimantec-suricata-node.conf
COPY config/docker/suricata-monitor.yaml /etc/phimantec/suricata-monitor.yaml
COPY config/suricata/local.rules /etc/suricata/rules/phimantec-local.rules
COPY suricata-monitor /opt/phimantec/suricata-monitor
COPY scripts/docker_suricata_node_entrypoint.sh /usr/local/bin/docker_suricata_node_entrypoint.sh
COPY scripts/phimantec_ingress_rules.sh /usr/local/bin/phimantec_ingress_rules.sh
COPY scripts/phimantec_suricata_yaml_patch.py /usr/local/bin/phimantec_suricata_yaml_patch.py
RUN chmod 0755 /usr/local/bin/docker_suricata_node_entrypoint.sh /usr/local/bin/phimantec_ingress_rules.sh /usr/local/bin/phimantec_suricata_yaml_patch.py \
 && chown -R phimantec:phimantec /opt/phimantec/suricata-monitor /var/lib/suricata-monitor /var/log/phimantec
EXPOSE 18080 18090 5050
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/docker_suricata_node_entrypoint.sh"]
