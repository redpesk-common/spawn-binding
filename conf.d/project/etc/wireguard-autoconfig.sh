#!/bin/bash
# reference https://www.wireguard.com/quickstart/

# Author: Fulup Ar Foll fulup@iot.bzh
# Date: jan-2021
# Object: create a dummy wireguard config to test autostart spawn-binding capabilities

WIREGUARD_DIR=/etc/wireguard
WIREGUARD_NAME=spawn-binding
WIREGUARD_CONF=$WIREGUARD_DIR/$WIREGUARD_NAME.conf

echo "Spwan-Binding 'autostart' wireguard config. test=>[wg-quick up $WIREGUARD_NAME]"

umask 077
mkdir -p $WIREGUARD_DIR
if test $? -ne 0; then
    echo "fail to create $WIREGUARD_DIR (use sudo)"
    exit 1;
fi

# add wireguard interface to our system (useless with wg-quick)
#ip link del dev wg0 2>/dev/null || true
#ip link add dev wg0 type wireguard

# Generate a dymmy config for test
echo "[Interface]" >$WIREGUARD_CONF
echo "#Unsecure wireguar test-config for spwan-binding autostart test=>[wg setconf wg0 $WIREGUARD_CONF]" >>$WIREGUARD_CONF
echo -n "PrivateKey = " >>$WIREGUARD_CONF
wg genkey >$WIREGUARD_DIR/autostart-privatekey
cat <$WIREGUARD_DIR/autostart-privatekey >>$WIREGUARD_CONF
echo "ListenPort = 51820" >>$WIREGUARD_CONF
echo ""  >>$WIREGUARD_CONF

echo "[Peer]" >>$WIREGUARD_CONF
echo -n "PublicKey = " >>$WIREGUARD_CONF
wg pubkey <$WIREGUARD_DIR/autostart-privatekey >$WIREGUARD_DIR/autostart-publicKey
cat <$WIREGUARD_DIR/autostart-publicKey >>$WIREGUARD_CONF
echo "Publickey endpoint 'localhost:1898' => `cat $WIREGUARD_DIR/autostart-publicKey`"

echo "Endpoint = localhost:18981" >>$WIREGUARD_CONF
echo "AllowedIPs =0.0.0.0/0" >>$WIREGUARD_CONF

# give owner right tp daemon user
chown daemon $WIREGUARD_DIR
