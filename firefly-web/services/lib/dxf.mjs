import DxfParser from "dxf-parser";
export function parseDxf(source) {
  if (
    typeof source !== "string" ||
    Buffer.byteLength(source) > 5 * 1024 * 1024 ||
    !source.includes("SECTION") ||
    !source.includes("EOF")
  )
    throw Error("Se requiere DXF ASCII válido de hasta 5 MiB");
  const dxf = new DxfParser().parseSync(source);
  if (!dxf?.entities?.length) throw Error("DXF sin entidades");
  if (dxf.entities.length > 20000) throw Error("Máximo 20.000 entidades");
  const shapes = [],
    warnings = new Set();
  const point = (v) => {
    if (v.z && Math.abs(v.z) > 1e-6)
      throw Error("DXF 3D no compatible: proyecte el plano a Z=0");
    return [Number(v.x), Number(v.y)];
  };
  for (const e of dxf.entities) {
    if (e.type === "LINE")
      shapes.push({ type: "path", points: e.vertices.map(point) });
    else if (["LWPOLYLINE", "POLYLINE"].includes(e.type)) {
      if (e.vertices.some((v) => v.bulge))
        warnings.add("Polilíneas con bulge: segmentos aproximados por líneas");
      shapes.push({
        type: "path",
        points: e.vertices.map(point),
        closed: !!e.shape,
      });
    } else if (e.type === "CIRCLE")
      shapes.push({
        type: "circle",
        center: point(e.center),
        radius: e.radius,
      });
    else if (e.type === "ARC") {
      point(e.center);
      const pts = [];
      let end = e.endAngle;
      while (end < e.startAngle) end += 2 * Math.PI;
      for (let i = 0; i <= 40; i++) {
        const a = e.startAngle + ((end - e.startAngle) * i) / 40;
        pts.push([
          e.center.x + e.radius * Math.cos(a),
          e.center.y + e.radius * Math.sin(a),
        ]);
      }
      shapes.push({ type: "path", points: pts });
    } else if (["TEXT", "MTEXT"].includes(e.type)) {
      warnings.add("Textos omitidos en visor geométrico");
    } else if (e.type === "INSERT")
      throw Error(
        "DXF con bloques INSERT o referencias externas: explote los bloques y guarde un DXF 2D antes de cargarlo",
      );
    else warnings.add("Entidad omitida: " + e.type);
  }
  if (!shapes.length)
    throw Error(
      "Sin geometría compatible: LINE, POLYLINE, LWPOLYLINE, ARC o CIRCLE",
    );
  const coords = shapes.flatMap((s) =>
    s.type === "circle"
      ? [
          [s.center[0] - s.radius, s.center[1] - s.radius],
          [s.center[0] + s.radius, s.center[1] + s.radius],
        ]
      : s.points,
  );
  if (
    coords.length > 100000 ||
    coords.some((p) => p.some((v) => !Number.isFinite(v) || Math.abs(v) > 1e9))
  )
    throw Error("Geometría fuera de límites");
  const xs = coords.map((p) => p[0]),
    ys = coords.map((p) => p[1]);
  const minX = Math.min(...xs),
    maxX = Math.max(...xs),
    minY = Math.min(...ys),
    maxY = Math.max(...ys);
  return {
    shapes,
    bounds: [minX, minY, Math.max(1, maxX - minX), Math.max(1, maxY - minY)],
    warnings: [...warnings],
  };
}
