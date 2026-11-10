#!/usr/bin/env python3
"""Run serial 30 s battery tests, with at least 5 min on AC per lamp."""

import argparse
from itertools import count
import re
import sys
import time

from csv_download import TransferError, synchronize, wait_repeat_ready

LAMPS = ('f96afdfc', '8e6dcd6a')
REST_SECONDS = 300


def receive_until(port, predicate, timeout, failure):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline(1024).decode('ascii', errors='replace').strip()
        if 'ESP-ROM:' in line:
            raise TransferError('El C6 se reinició; no se enviarán más pruebas.')
        if 'MANUAL_TEST error:' in line:
            raise TransferError(line)
        if predicate(line):
            return line
    if failure is not None:
        raise TransferError(failure)


def sample_matches(line, device_id, ac, active):
    return (' RX short=' in line and f'node={device_id} ' in line and
            f' AC={ac} ' in line and f' self_test={active} ' in line and
            ' sensor_valid=1 ' in line)


def run_test(port, device_id):
    receive_until(port, lambda s: sample_matches(s, device_id, 1, 0), 80,
                  f'{device_id}: sin telemetría válida en AC; no se inició la prueba.')
    port.write(f'zigbee test {device_id}\n'.encode('ascii'))
    receive_until(port, lambda s: sample_matches(s, device_id, 0, 1), 24,
                  f'{device_id}: no confirmó el inicio; no se reenviará la orden.')
    print(f'{device_id}: prueba en batería iniciada.', flush=True)

    returned_at = None
    report = None

    def completed(line):
        nonlocal returned_at, report
        if sample_matches(line, device_id, 1, 0) and returned_at is None:
            returned_at = time.monotonic()
        if (' TEST short=' in line and f'node={device_id} ' in line and ' id=0 ' in line and
                re.search(r'\bduration=30_s samples=[1-9]\d*\b', line)):
            report = line
        return returned_at is not None and report is not None

    receive_until(port, completed, 65,
                  f'{device_id}: no se confirmó el regreso a AC y el reporte de 30 s.')
    print(report, flush=True)
    print(f'{device_id}: regreso a AC confirmado; comienza el reposo.', flush=True)
    return returned_at + REST_SECONDS


def run(port, cycles):
    print(synchronize(port).decode('ascii'), flush=True)
    if ' phase=off ' not in wait_repeat_ready(port):
        raise TransferError('La repetición del C6 está activa; detenla con repeat-stop antes de continuar.')

    due = dict.fromkeys(LAMPS, 0.0)
    for _ in (range(cycles) if cycles is not None else count()):
        for device_id in LAMPS:
            wait = due[device_id] - time.monotonic()
            if wait > 0:
                print(f'{device_id}: faltan {wait:.0f} s de reposo.', flush=True)
                receive_until(port, lambda _: False, wait, None)
            due[device_id] = run_test(port, device_id)
    # A finite run includes the last rest period, without initiating more tests.
    wait = max(due.values()) - time.monotonic()
    if wait > 0:
        print(f'Último reposo: {wait:.0f} s restantes.', flush=True)
        receive_until(port, lambda _: False, wait, None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/ttyACM0')
    parser.add_argument('--cycles', type=int, help='Ciclos por lámpara; si se omite, continúa hasta Ctrl+C')
    args = parser.parse_args()
    if args.cycles is not None and args.cycles < 1:
        parser.error('--cycles debe ser mayor que cero')
    try:
        import serial
    except ImportError:
        parser.error('Instala pyserial: python3 -m pip install -r tools/requirements.txt')
    try:
        port = serial.Serial(baudrate=115200, timeout=.3, write_timeout=3, exclusive=True)
        port.dtr = False
        port.rts = False
        port.port = args.port
        with port:
            run(port, args.cycles)
    except KeyboardInterrupt:
        print('\nDetenido. El H2 termina por sí solo cualquier prueba que ya empezó.')
    except (OSError, TransferError) as error:
        print(f'Ensayo detenido: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
