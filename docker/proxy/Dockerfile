# PHIMANTEC C11 Proxy runtime image.
# Ubuntu 24.04 profile with fail-fast APT and no container hardening surprises.

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
ENV DEBIAN_FRONTEND=noninteractive
COPY docker/common/apt_install.sh /usr/local/bin/phimantec_apt_install
RUN /usr/local/bin/phimantec_apt_install ca-certificates openssl tini \
 && groupadd --system phimantec \
 && useradd --system --no-create-home --shell /usr/sbin/nologin --gid phimantec phimantec \
 && install -d -o phimantec -g phimantec -m 0750 /var/cache/phimantec/certs \
 && install -d -o root -g phimantec -m 0750 /etc/phimantec/ca
COPY --from=builder /src/build/phimantec-proxyd /usr/local/bin/phimantec-proxyd
COPY --from=builder /src/build/phimantec-flowbus-probe /usr/local/bin/phimantec-flowbus-probe
COPY config/docker/phimantec-proxyd.conf /etc/phimantec/phimantec-proxyd.conf
COPY scripts/docker_proxy_entrypoint.sh /usr/local/bin/docker_proxy_entrypoint.sh
RUN chmod 0755 /usr/local/bin/docker_proxy_entrypoint.sh
EXPOSE 3128
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/docker_proxy_entrypoint.sh"]
