###############################################################################
# Stage 1: Build FreeSWITCH + mod_battlecity
###############################################################################
FROM debian:bookworm AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential autoconf automake libtool libtool-bin pkg-config gawk \
    git ca-certificates cmake \
    libssl-dev libcurl4-openssl-dev libsqlite3-dev libedit-dev \
    libldns-dev libpcre3-dev libtiff-dev libspeex-dev libspeexdsp-dev \
    libjpeg-dev libncurses5-dev libopus-dev libsndfile1-dev \
    liblua5.2-dev libavformat-dev libswscale-dev libswresample-dev \
    yasm nasm uuid-dev zlib1g-dev \
    python3 \
    # GStreamer (needed for mod_battlecity)
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    # Cairo (referenced in Makefile)
    libcairo2-dev \
    && rm -rf /var/lib/apt/lists/*

# Build spandsp3 from FreeSWITCH fork (Debian only has spandsp 0.0.6, FS needs >= 3.0)
WORKDIR /usr/src
RUN git clone --depth 1 https://github.com/freeswitch/spandsp.git spandsp && \
    cd spandsp && \
    ./bootstrap.sh && \
    ./configure --prefix=/usr && \
    make -j$(nproc) && \
    make install && \
    ldconfig

# Build sofia-sip from FreeSWITCH fork (Debian has older version, FS needs >= 1.13.17)
RUN git clone --depth 1 https://github.com/freeswitch/sofia-sip.git sofia-sip && \
    cd sofia-sip && \
    ./bootstrap.sh && \
    ./configure --prefix=/usr && \
    make -j$(nproc) && \
    make install && \
    ldconfig

# Clone FreeSWITCH
RUN git clone --depth 1 -b v1.10 https://github.com/signalwire/freeswitch.git freeswitch

# Copy mod_battlecity into the source tree
COPY . /usr/src/freeswitch/src/mod/applications/mod_battlecity/

# Replace modules.conf.in with minimal set (avoid libks/verto/signalwire deps)
RUN printf '%s\n' \
    "loggers/mod_console" \
    "loggers/mod_logfile" \
    "endpoints/mod_sofia" \
    "dialplans/mod_dialplan_xml" \
    "codecs/mod_g711" \
    "codecs/mod_h26x" \
    "codecs/mod_opus" \
    "applications/mod_commands" \
    "applications/mod_dptools" \
    "applications/mod_battlecity" \
    "event_handlers/mod_event_socket" \
    > /usr/src/freeswitch/build/modules.conf.in

# Build FreeSWITCH
WORKDIR /usr/src/freeswitch
RUN ./bootstrap.sh -j
RUN ./configure --prefix=/usr/local/freeswitch \
    --enable-core-pgsql-support=no \
    --enable-static-v8=no
RUN make -j$(nproc)
RUN make install

# Install vanilla config from source tree (FS v1.10 reads from etc/freeswitch/)
RUN mkdir -p /usr/local/freeswitch/conf && \
    cp -a /usr/src/freeswitch/conf/vanilla/* /usr/local/freeswitch/conf/ && \
    mkdir -p /usr/local/freeswitch/etc/freeswitch && \
    cp -a /usr/src/freeswitch/conf/vanilla/* /usr/local/freeswitch/etc/freeswitch/

# Patch BOTH config locations (conf/ and etc/freeswitch/)
RUN for CONFDIR in /usr/local/freeswitch/conf /usr/local/freeswitch/etc/freeswitch; do \
      # Add mod_battlecity to modules
      sed -i '/<\/modules>/i\    <load module="mod_battlecity"/>' \
        $CONFDIR/autoload_configs/modules.conf.xml && \
      # Enable mod_h26x (video codecs)
      sed -i 's/<!--<load module="mod_h26x"\/>-->/<load module="mod_h26x"\/>/' \
        $CONFDIR/autoload_configs/modules.conf.xml && \
      # Add battlecity dialplan extension
      sed -i '/<context name="default">/a\    <extension name="battlecity"><condition field="destination_number" expression="^9999$"><action application="set" data="rtp_secure_media=false"/><action application="play_battlecity" data=""/></condition></extension>' \
        $CONFDIR/dialplan/default.xml && \
      # Set external IP - use Docker host IP so SDP contains routable address
      # Override with EXTERNAL_IP env var at runtime, default to auto-detect
      sed -i 's|stun:stun.freeswitch.org|$${local_ip_v4}|g' \
        $CONFDIR/vars.xml ; \
      # Set ext-rtp-ip and ext-sip-ip to use external_rtp_ip/external_sip_ip vars
      # These will be overridden by the entrypoint script
      true ; \
      # Disable default password warning in dialplan (it blocks all calls)
      sed -i 's|"${default_password}" expression="^1234$"|"${default_password}" expression="^DISABLED$"|' \
        $CONFDIR/dialplan/default.xml && \
      # Fix event_socket IPv6
      sed -i 's|value="::"|value="0.0.0.0"|g' \
        $CONFDIR/autoload_configs/event_socket.conf.xml 2>/dev/null ; \
      # Disable SRTP/DTLS completely (DTLS handshake blocks on Docker UDP)
      sed -i '/<\/settings>/i\    <param name="rtp-secure-media" value="forbidden"/>\n    <param name="rtp-secure-media-inbound" value="forbidden"/>\n    <param name="rtp-secure-media-outbound" value="forbidden"/>' \
        $CONFDIR/sip_profiles/internal.xml 2>/dev/null ; \
      sed -i 's|internal_ssl_enable=true|internal_ssl_enable=false|' \
        $CONFDIR/vars.xml 2>/dev/null ; \
      sed -i 's|external_ssl_enable=true|external_ssl_enable=false|' \
        $CONFDIR/vars.xml 2>/dev/null ; \
    done && \
    # Remove DTLS certificates so FS physically cannot do DTLS handshake
    rm -f /usr/local/freeswitch/etc/freeswitch/tls/dtls-srtp.pem \
          /usr/local/freeswitch/etc/freeswitch/tls/wss.pem \
          /usr/local/freeswitch/conf/tls/dtls-srtp.pem \
          /usr/local/freeswitch/conf/tls/wss.pem 2>/dev/null; true

###############################################################################
# Stage 2: Runtime
###############################################################################
FROM debian:bookworm-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libssl3 libcurl4 libsqlite3-0 libedit2 \
    libldns3 libpcre3 libtiff6 libspeex1 libspeexdsp1 \
    libjpeg62-turbo libncurses6 libopus0 libsndfile1 \
    liblua5.2-0 uuid \
    # GStreamer runtime
    libgstreamer1.0-0 libgstreamer-plugins-base1.0-0 \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    libcairo2 \
    && rm -rf /var/lib/apt/lists/*

# Copy FreeSWITCH installation (includes patched config) + shared libs
COPY --from=builder /usr/local/freeswitch /usr/local/freeswitch
COPY --from=builder /usr/lib/libspandsp* /usr/lib/
COPY --from=builder /usr/lib/libsofia-sip-ua* /usr/lib/
RUN ldconfig

# Symlink for convenience
RUN ln -s /usr/local/freeswitch/bin/freeswitch /usr/local/bin/freeswitch && \
    ln -s /usr/local/freeswitch/bin/fs_cli /usr/local/bin/fs_cli

# SIP/RTP ports
EXPOSE 5060/udp 5060/tcp 5080/udp 5080/tcp
# RTP port range
EXPOSE 16384-32768/udp

COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
