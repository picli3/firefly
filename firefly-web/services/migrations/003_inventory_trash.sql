ALTER TABLE locations ADD COLUMN deleted_at timestamptz;
ALTER TABLE gateways ADD COLUMN deleted_at timestamptz;
ALTER TABLE lamps ADD COLUMN deleted_at timestamptz;

-- Lock referenced rows when assigning/restoring inventory. A concurrent trash
-- operation must wait, then sees the new dependent before it can succeed.
CREATE FUNCTION guard_inventory_trash() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF NEW.deleted_at IS NOT NULL THEN
    IF TG_OP = 'UPDATE' AND OLD.deleted_at IS NULL THEN
      IF TG_TABLE_NAME = 'locations' THEN
        IF EXISTS(SELECT 1 FROM locations WHERE parent_id=NEW.id AND deleted_at IS NULL)
          OR EXISTS(SELECT 1 FROM gateways WHERE (location_id=NEW.id OR map_level_id=NEW.id) AND deleted_at IS NULL)
          OR EXISTS(SELECT 1 FROM lamps WHERE location_id=NEW.id AND deleted_at IS NULL) THEN
          RAISE EXCEPTION 'Retire primero las ubicaciones y dispositivos dependientes' USING ERRCODE='23514';
        END IF;
      ELSIF TG_TABLE_NAME = 'gateways' THEN
        IF EXISTS(SELECT 1 FROM lamps WHERE gateway_id=NEW.id AND deleted_at IS NULL) THEN
          RAISE EXCEPTION 'Retire primero las lámparas del gateway' USING ERRCODE='23514';
        END IF;
      END IF;
    END IF;
    RETURN NEW;
  END IF;
  IF TG_TABLE_NAME = 'locations' THEN
    IF NEW.parent_id IS NOT NULL THEN
      PERFORM 1 FROM locations WHERE id=NEW.parent_id AND deleted_at IS NULL FOR SHARE;
      IF NOT FOUND THEN RAISE EXCEPTION 'Restaure primero la ubicación superior' USING ERRCODE='23514'; END IF;
    END IF;
  ELSIF TG_TABLE_NAME = 'gateways' THEN
    IF NEW.location_id IS NOT NULL THEN
      PERFORM 1 FROM locations WHERE id=NEW.location_id AND deleted_at IS NULL FOR SHARE;
      IF NOT FOUND THEN RAISE EXCEPTION 'Restaure primero la ubicación del gateway' USING ERRCODE='23514'; END IF;
    END IF;
    IF NEW.map_level_id IS NOT NULL THEN
      PERFORM 1 FROM locations WHERE id=NEW.map_level_id AND deleted_at IS NULL FOR SHARE;
      IF NOT FOUND THEN RAISE EXCEPTION 'Restaure primero el nivel del gateway' USING ERRCODE='23514'; END IF;
    END IF;
  ELSIF TG_TABLE_NAME = 'lamps' THEN
    PERFORM 1 FROM gateways WHERE id=NEW.gateway_id AND deleted_at IS NULL FOR SHARE;
    IF NOT FOUND THEN RAISE EXCEPTION 'Restaure primero el gateway de la lámpara' USING ERRCODE='23514'; END IF;
    IF NEW.location_id IS NOT NULL THEN
      PERFORM 1 FROM locations WHERE id=NEW.location_id AND deleted_at IS NULL FOR SHARE;
      IF NOT FOUND THEN RAISE EXCEPTION 'Restaure primero el nivel de la lámpara' USING ERRCODE='23514'; END IF;
    END IF;
  END IF;
  RETURN NEW;
END $$;
CREATE TRIGGER locations_trash_guard BEFORE INSERT OR UPDATE OF deleted_at,parent_id ON locations FOR EACH ROW EXECUTE FUNCTION guard_inventory_trash();
CREATE TRIGGER gateways_trash_guard BEFORE INSERT OR UPDATE OF deleted_at,location_id,map_level_id ON gateways FOR EACH ROW EXECUTE FUNCTION guard_inventory_trash();
CREATE TRIGGER lamps_trash_guard BEFORE INSERT OR UPDATE OF deleted_at,location_id,gateway_id ON lamps FOR EACH ROW EXECUTE FUNCTION guard_inventory_trash();
