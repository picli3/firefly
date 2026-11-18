ALTER TABLE gateways ADD COLUMN test_capable boolean NOT NULL DEFAULT false;
CREATE TABLE test_requests(
 id uuid PRIMARY KEY,
 lamp_id uuid NOT NULL REFERENCES lamps(id),
 gateway_id text NOT NULL REFERENCES gateways(id),
 test_id integer NOT NULL CHECK(test_id BETWEEN 32768 AND 65535),
 duration_s integer NOT NULL DEFAULT 30 CHECK(duration_s=30),
 status text NOT NULL DEFAULT 'pending' CHECK(status IN ('pending','sent','accepted','completed','rejected','unknown','expired','cancelled')),
 requested_by uuid REFERENCES users(id),
 created_at timestamptz NOT NULL DEFAULT clock_timestamp(),
 expires_at timestamptz NOT NULL DEFAULT clock_timestamp()+interval '30 seconds',
 deadline_at timestamptz NOT NULL DEFAULT clock_timestamp()+interval '3 minutes',
 sent_at timestamptz, acknowledged_at timestamptz, completed_at timestamptz,
 reason text,
 result jsonb
);
CREATE UNIQUE INDEX one_manual_test_per_gateway ON test_requests(gateway_id) WHERE status IN ('pending','sent','accepted');
CREATE INDEX test_request_lamp ON test_requests(lamp_id,created_at DESC);

CREATE UNIQUE INDEX manual_test_id_never_reused ON test_requests(gateway_id,test_id);
