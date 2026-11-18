#!/usr/bin/env python3
"""Import a Firefly gateway JSON into local, ignored ESP-IDF configuration."""
import argparse
import json
import os
from pathlib import Path
import re
import ssl
from urllib.parse import urlsplit


def configure(source, project):
    bundle = json.loads(source.read_text())
    config = bundle.get('config', bundle)
    if not isinstance(config, dict) or config.get('schema_version') != 1:
        raise ValueError('Expected Firefly schema_version 1 configuration')
    mqtt = config['mqtt']
    gateway = config['gateway_id']
    if not isinstance(gateway, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', gateway):
        raise ValueError('Invalid gateway_id')
    if config.get('network') != {'interface': 'ethernet', 'addressing': 'dhcp'}:
        raise ValueError('This firmware supports Ethernet/DHCP configuration only')
    uri = urlsplit(mqtt['uri'])
    if (uri.scheme != 'mqtts' or not uri.hostname or uri.username or uri.password
            or uri.path not in ('', '/') or uri.query or uri.fragment
            or uri.hostname in ('localhost', '127.0.0.1', '::1')
            or uri.hostname != mqtt['host'] or uri.port != mqtt['port']):
        raise ValueError('Expected a reachable mqtts:// host and matching port without embedded credentials')
    if mqtt.get('tls') is not True or mqtt.get('verify_certificate') is not True:
        raise ValueError('Verified TLS is required')
    if mqtt.get('username') != gateway or not isinstance(mqtt.get('password'), str) or not mqtt['password']:
        raise ValueError('Expected gateway username and nonempty password')
    if mqtt.get('protocol_version') != '3.1.1' or mqtt.get('qos') != 1:
        raise ValueError('Expected MQTT 3.1.1 with QoS 1')
    root = f'firefly/v1/gateways/{gateway}'
    if mqtt.get('topic_prefix') != root:
        raise ValueError('Unexpected Firefly topic prefix')
    for key, suffix in {'status': 'status', 'telemetry': 'telemetry', 'test_report': 'test-report',
                        'configuration_ack': 'config-ack', 'configuration_command': 'commands/config'}.items():
        if mqtt.get('topics', {}).get(key) != root + '/' + suffix:
            raise ValueError('Unexpected Firefly topic contract')
    pem = mqtt['ca_pem']
    if 'PRIVATE KEY' in pem:
        raise ValueError('Expected public certificate, never a private key')
    ssl.PEM_cert_to_DER_cert(pem)
    # Build a context now to reject malformed certificates before changing files.
    ssl.create_default_context(cadata=pem)
    ntp = config.get('time', {}).get('ntp_servers', [])
    if not ntp or not isinstance(ntp[0], str) or not re.fullmatch(r'[A-Za-z0-9.:-]{1,253}', ntp[0]):
        raise ValueError('Missing valid NTP server')
    keepalive, heartbeat = mqtt['keepalive_s'], mqtt['heartbeat_interval_s']
    if (type(keepalive) is not int or not 5 <= keepalive <= 3600
            or type(heartbeat) is not int or not 5 <= heartbeat <= 86400):
        raise ValueError('Invalid MQTT timing')
    client = mqtt['client_id']
    if not isinstance(client, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,128}', client):
        raise ValueError('Invalid MQTT client ID')
    for value in (mqtt['password'],):
        if any(ord(c) < 32 or ord(c) > 126 for c in value):
            raise ValueError('Credentials must use printable ASCII')
    sdkconfig = project / 'sdkconfig'
    if not sdkconfig.is_file():
        raise ValueError('Run idf.py reconfigure once before importing')
    text = sdkconfig.read_text()
    ca_file = project / 'mqtt-ca.local.pem'
    values = {
        'GATEWAY_ETH_ENABLED': 'y', 'GATEWAY_MQTT_TLS': 'y',
        'GATEWAY_MQTT_URI': json.dumps(mqtt['uri']),
        'GATEWAY_MQTT_CA_FILE': json.dumps(str(ca_file)),
        'GATEWAY_NTP_SERVER': json.dumps(ntp[0]),
        'GATEWAY_MQTT_GATEWAY_ID': json.dumps(gateway),
        'GATEWAY_MQTT_CLIENT_ID': json.dumps(client),
        'GATEWAY_MQTT_USERNAME': json.dumps(mqtt['username']),
        'GATEWAY_MQTT_PASSWORD': json.dumps(mqtt['password']),
        'GATEWAY_MQTT_KEEPALIVE_SECONDS': str(keepalive),
        'GATEWAY_MQTT_HEARTBEAT_SECONDS': str(heartbeat),
        'MQTT_USE_CUSTOM_CONFIG': 'y', 'MQTT_OUTBOX_EXPIRED_TIMEOUT_MS': '15000',
    }
    for key, value in values.items():
        pattern = r'(?m)^(?:CONFIG_' + key + r'=.*|# CONFIG_' + key + r' is not set)$'
        replacement = 'CONFIG_' + key + '=' + value
        if re.search(pattern, text):
            text = re.sub(pattern, lambda match: replacement, text)
        else:
            text += '\n' + replacement + '\n'
    # No secrets are printed or placed in tracked defaults.
    ca_file.write_text(pem)
    os.chmod(sdkconfig, 0o600)
    sdkconfig.write_text(text)
    print(f'Imported verified-TLS configuration for gateway {gateway}; credentials remain local.')
    print('Build and flash from this ESP-IDF project. Battery configuration requires a compatible H2 v2.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('json_file', type=Path)
    args = parser.parse_args()
    try:
        configure(args.json_file, Path(__file__).resolve().parents[1])
    except (KeyError, ValueError, TypeError, OSError) as exc:
        # Avoid echoing malformed JSON values, which can include credentials.
        parser.exit(1, f'Configuration rejected ({type(exc).__name__}); check the Firefly JSON contract.\n')
