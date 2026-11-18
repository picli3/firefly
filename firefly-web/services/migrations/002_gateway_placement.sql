ALTER TABLE gateways ADD COLUMN IF NOT EXISTS map_level_id uuid REFERENCES locations(id);
ALTER TABLE gateways ADD COLUMN IF NOT EXISTS x double precision;
ALTER TABLE gateways ADD COLUMN IF NOT EXISTS y double precision;
ALTER TABLE gateways ADD CONSTRAINT gateway_coordinates_pair CHECK ((x IS NULL) = (y IS NULL));
ALTER TABLE gateways ADD CONSTRAINT gateway_coordinates_level CHECK (map_level_id IS NOT NULL OR x IS NULL);
ALTER TABLE gateways ADD CONSTRAINT gateway_coordinates_bounds CHECK (x IS NULL OR (x BETWEEN -1000000000 AND 1000000000 AND y BETWEEN -1000000000 AND 1000000000));
UPDATE gateways g SET map_level_id=l.id FROM locations l WHERE g.location_id=l.id AND l.kind='level' AND g.map_level_id IS NULL;
