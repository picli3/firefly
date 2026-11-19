-- Planificación de pruebas por lámpara y duraciones de prueba de 30 a 300 s.

-- Duración máxima que anuncia cada gateway (status.test_max_duration_s).
-- Firmware anterior no la envía: se asume 30 s.
ALTER TABLE gateways ADD COLUMN test_max_duration_s integer NOT NULL DEFAULT 30
  CHECK (test_max_duration_s BETWEEN 30 AND 300);

ALTER TABLE test_requests DROP CONSTRAINT IF EXISTS test_requests_duration_s_check;
ALTER TABLE test_requests ADD CONSTRAINT test_requests_duration_s_check
  CHECK (duration_s BETWEEN 30 AND 300);
-- Origen de la solicitud: botón manual o planificación.
ALTER TABLE test_requests ADD COLUMN source text NOT NULL DEFAULT 'manual'
  CHECK (source IN ('manual', 'schedule'));

-- Una planificación por lámpara: días ISO (1 = lunes … 7 = domingo), hora local
-- y duración. La hora se interpreta en `timezone`.
CREATE TABLE test_schedules(
  lamp_id uuid PRIMARY KEY REFERENCES lamps(id),
  enabled boolean NOT NULL DEFAULT true,
  days smallint[] NOT NULL CHECK (
    cardinality(days) BETWEEN 1 AND 7
    AND days <@ ARRAY[1, 2, 3, 4, 5, 6, 7]::smallint[]
  ),
  time_local time NOT NULL,
  duration_s integer NOT NULL CHECK (duration_s BETWEEN 30 AND 300),
  timezone text NOT NULL DEFAULT 'America/Lima',
  updated_by uuid REFERENCES users(id),
  updated_at timestamptz NOT NULL DEFAULT now()
);

-- Cada ocurrencia programada: se reintenta durante 15 minutos y queda como
-- 'requested' (se creó la solicitud de prueba) u 'skipped' (motivo en reason).
CREATE TABLE test_schedule_runs(
  lamp_id uuid NOT NULL REFERENCES lamps(id),
  slot timestamptz NOT NULL,
  status text NOT NULL DEFAULT 'waiting'
    CHECK (status IN ('waiting', 'requested', 'skipped')),
  duration_s integer NOT NULL CHECK (duration_s BETWEEN 30 AND 300),
  request_id uuid REFERENCES test_requests(id),
  reason text,
  attempts integer NOT NULL DEFAULT 0,
  updated_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (lamp_id, slot)
);
CREATE INDEX test_schedule_runs_waiting ON test_schedule_runs(slot) WHERE status = 'waiting';
