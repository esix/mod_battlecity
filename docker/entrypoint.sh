#!/bin/bash
# If EXTERNAL_IP is set, patch FreeSWITCH config to use it in SDP
# This is needed when running in Docker so SDP contains the host's routable IP
if [ -n "$EXTERNAL_IP" ]; then
    echo "Setting external IP to $EXTERNAL_IP"
    for CONFDIR in /usr/local/freeswitch/conf /usr/local/freeswitch/etc/freeswitch; do
        if [ -f "$CONFDIR/vars.xml" ]; then
            sed -i "s|external_rtp_ip=.*\"|external_rtp_ip=$EXTERNAL_IP\"|" "$CONFDIR/vars.xml"
            sed -i "s|external_sip_ip=.*\"|external_sip_ip=$EXTERNAL_IP\"|" "$CONFDIR/vars.xml"
        fi
    done
fi

exec /usr/local/freeswitch/bin/freeswitch -nonat -nf "$@"
