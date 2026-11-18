import PDFDocument from "pdfkit";
import SVGtoPDF from "svg-to-pdfkit";
import { resolve } from "node:path";
import { readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { buildReportModel, reportDate } from "./report-model.mjs";

const C = {
  green: "#173f33",
  table: "#1c422c",
  lime: "#b7df6b",
  line: "#c6ddce",
  light: "#f3f8f4",
  ink: "#243248",
  muted: "#5f7490",
  white: "#ffffff",
};
const X = 51,
  W = 493,
  RIGHT = X + W;
const format = (n, d = 1) =>
  n === null || n === undefined
    ? "-"
    : Number(n).toLocaleString("es-PE", {
        minimumFractionDigits: d,
        maximumFractionDigits: d,
      });
const stamp = (d) =>
  new Date(d).toLocaleString("es-PE", {
    timeZone: "America/Lima",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    hour12: false,
  });
const place = (r) =>
  [
    r.location_snapshot?.site,
    r.location_snapshot?.building,
    r.location_snapshot?.level,
    r.location_snapshot?.place,
  ]
    .filter(Boolean)
    .join(" / ") || "Sin ubicación registrada";
const profile = (r) =>
  `${r.config_version ?? "desconocida"} / ${r.profile_snapshot?.name || "perfil no confirmado"}`;

export async function pdfReport(measurements, tests, filters, context = {}) {
  const model = buildReportModel(measurements, tests, filters, context);
  const logo = await readFile(
    resolve(process.env.PDF_ASSET_DIR || "services/assets", "firefly.svg"),
    "utf8",
  );
  const doc = new PDFDocument({
      size: "A4",
      margin: 0,
      bufferPages: true,
      autoFirstPage: false,
      info: {
        Title: "Firefly - Reporte de Desempeño Mensual",
        Author: model.preparedBy,
        Subject: "Supervisión de luminarias y resultados de pruebas recibidos",
      },
    }),
    chunks = [];
  doc.registerFont(
    "Body",
    resolve(process.env.PDF_FONT_DIR || "services/assets", "DejaVuSans.ttf"),
  );
  doc.registerFont(
    "Bold",
    resolve(
      process.env.PDF_FONT_DIR || "services/assets",
      "DejaVuSans-Bold.ttf",
    ),
  );
  const done = new Promise((resolve, reject) => {
    doc.on("data", (c) => chunks.push(c));
    doc.on("end", () => resolve(Buffer.concat(chunks)));
    doc.on("error", reject);
  });
  function text(
    s,
    x,
    y,
    width,
    size = 8,
    bold = false,
    color = C.ink,
    extra = {},
  ) {
    doc
      .font(bold ? "Bold" : "Body")
      .fontSize(size)
      .fillColor(color)
      .text(String(s), x, y, { width, lineGap: 1, ...extra });
  }
  function rect(x, y, w, h, fill, stroke = C.line) {
    doc.save().rect(x, y, w, h).fillAndStroke(fill, stroke).restore();
  }
  function line(x, y, w, color = C.line) {
    doc
      .save()
      .strokeColor(color)
      .lineWidth(0.6)
      .moveTo(x, y)
      .lineTo(x + w, y)
      .stroke()
      .restore();
  }
  function section(title, y) {
    text(title, X, y, W, 12, true, C.green);
    line(X, y + 18, W, C.lime);
  }
  function page() {
    doc.addPage();
  }
  function banner() {
    rect(X, 34, W, 119, C.green, C.green);
    SVGtoPDF(doc, logo.replaceAll("#000000", C.lime), 85, 50, {
      width: 65,
      height: 78,
      preserveAspectRatio: "xMidYMid meet",
    });
    text("Firefly", 167, 67, 290, 36, true, C.lime);
  }
  const code =
    "FF-" +
    model.generatedAt.toISOString().slice(0, 10).replaceAll("-", "") +
    "-" +
    createHash("sha256")
      .update(JSON.stringify(filters || {}) + model.generatedAt.toISOString())
      .digest("hex")
      .slice(0, 6)
      .toUpperCase();
  function metadata(year) {
    text("Reporte de Desempeño Mensual", X, 244, 386, 16.6, true, C.green);
    text("Código: " + code, RIGHT - 133, 252, 133, 7.2, true, C.muted, {
      align: "right",
    });
    line(X, 267, W, C.green);
    const widths = [70, 179, 63, 181],
      labels = [
        [
          "Período",
          model.years.length > 1 ? year + " (período filtrado)" : model.period,
          "Área",
          model.scope,
        ],
        ["Responsable", model.preparedBy, "Emisión", stamp(model.generatedAt)],
      ];
    for (let row = 0; row < 2; row++) {
      let x = X;
      for (let col = 0; col < 4; col++) {
        const w = widths[col];
        rect(x, 273 + row * 20, w, 20, col % 2 === 0 ? C.light : C.white);
        text(
          labels[row][col],
          x + 5,
          277 + row * 20,
          w - 10,
          8,
          col % 2 === 0,
          col % 2 === 0 ? C.green : C.ink,
          { height: 15, ellipsis: true },
        );
        x += w;
      }
    }
  }
  function chart(year) {
    const y = 353,
      h = 104,
      leftW = 318,
      rightX = X + 325,
      rightW = W - 325;
    rect(X, y, leftW, h, "#fbfdfb");
    rect(rightX, y, rightW, h, "#fbfdfb");
    text(
      "Evolución Mensual (Ene - Dic)",
      X + 8,
      y + 5,
      230,
      9.3,
      true,
      C.green,
    );
    text("PASS informado (%)", X + 203, y + 7, 105, 6, false, C.muted, {
      align: "right",
    });
    const px = X + 37,
      py = y + 25,
      pw = 270,
      ph = 58;
    for (const [v, label] of [
      [0, "0%"],
      [50, "50%"],
      [100, "100%"],
    ]) {
      const yy = py + ph - (v * ph) / 100;
      text(label, X + 3, yy - 3, 29, 6, false, C.muted, { align: "right" });
      doc
        .save()
        .dash(2, { space: 2 })
        .strokeColor("#dbe4e7")
        .lineWidth(0.5)
        .moveTo(px, yy)
        .lineTo(px + pw, yy)
        .stroke()
        .restore();
    }
    for (let m = 0; m < 12; m++) {
      const b = year.months[m],
        xx = px + m * (pw / 12) + 4;
      if (b.passRate !== null) {
        const hh = (b.passRate * ph) / 100;
        doc
          .save()
          .rect(xx, py + ph - hh, 12, Math.max(0.8, hh))
          .fillAndStroke("#e5f0e7", C.green)
          .restore();
      } else
        text("-", xx, py + ph - 9, 12, 7, false, C.muted, { align: "center" });
      text(b.name.slice(0, 3), xx - 3, py + ph + 4, 20, 6.2, false, C.muted, {
        align: "center",
      });
    }
    if (!year.total.tests)
      text(
        "Sin pruebas recibidas",
        px + 23,
        py + 20,
        pw - 46,
        9,
        false,
        C.muted,
        { align: "center" },
      );
    text(
      "Sin meta definida. Porcentaje de reportes PASS; no certifica autonomía.",
      X + 8,
      y + h - 9,
      leftW - 16,
      5.6,
      false,
      C.muted,
    );
    text(
      "Comparativa por Categoría",
      rightX + 8,
      y + 5,
      rightW - 16,
      9,
      true,
      C.green,
    );
    for (let i = 0; i < 4; i++) {
      const cat = year.categories[i],
        yy = y + 29 + i * 15;
      text(cat.label, rightX + 8, yy - 2, 76, 6.4);
      rect(rightX + 85, yy, 47, 7, "#ecf2ed", "#ecf2ed");
      if (cat.pct !== null && cat.count)
        doc
          .save()
          .rect(rightX + 85, yy, Math.max(0.4, (47 * cat.pct) / 100), 7)
          .fill([C.green, "#417c56", "#83c958", "#c8e0cc"][i])
          .restore();
      text(
        cat.pct === null ? "-" : format(cat.pct, 0) + "%",
        rightX + 134,
        yy - 3,
        rightW - 140,
        6.2,
        false,
        C.ink,
        { align: "right" },
      );
    }
    text(
      year.total.tests + " reportes recibidos",
      rightX + 8,
      y + h - 9,
      rightW - 16,
      5.8,
      false,
      C.muted,
    );
  }
  function monthlyTable(year) {
    const widths = [94, 82, 82, 81, 82, 72],
      headers = [
        "Mes",
        "Voltaje\n(V)",
        "Corriente\n(mA)",
        "Temperatura\n(°C)",
        "PASS\ninformado (%)",
        "Var. PASS\n(pp)",
      ];
    const y = 499,
      headH = 32,
      rowH = 16.8;
    let x = X;
    for (let i = 0; i < widths.length; i++) {
      rect(x, y, widths[i], headH, C.table, C.table);
      text(headers[i], x + 5, y + 6, widths[i] - 10, 7.5, true, C.white, {
        align: i ? "right" : "left",
      });
      x += widths[i];
    }
    line(X, y + headH, W, C.lime);
    const rows = [
      ...year.months,
      { ...year.total, name: "Promedio del período", variation: null },
    ];
    rows.forEach((b, index) => {
      const yy = y + headH + index * rowH,
        summary = index === 12;
      let xx = X;
      const values = [
        b.name,
        format(b.voltage, 3),
        format(b.current, 1),
        format(b.temperature, 1),
        format(b.passRate, 1),
        b.variation === null
          ? "-"
          : (b.variation > 0 ? "+" : "") + format(b.variation, 1),
      ];
      for (let col = 0; col < widths.length; col++) {
        rect(
          xx,
          yy,
          widths[col],
          rowH,
          summary ? "#e5f1e7" : index % 2 ? "#f7faf7" : C.white,
        );
        text(
          values[col],
          xx + 5,
          yy + 3,
          widths[col] - 10,
          col === 0 && summary ? 6.1 : 7.5,
          col === 0 || summary,
          summary ? C.green : C.ink,
          { align: col ? "right" : "left", height: 12 },
        );
        xx += widths[col];
      }
    });
    text(
      'Promedios de muestras válidas; sin datos = "-". PASS: resultado original. pp: puntos porcentuales.',
      X,
      y + headH + 13 * rowH + 7,
      W,
      6.2,
      false,
      C.muted,
    );
  }
  for (const year of model.years) {
    page();
    banner();
    metadata(year.year);
    section("1. Gráficos de Seguimiento", 328);
    chart(year);
    section("2. Tabla de Promedios Mensuales", 474);
    monthlyTable(year);
  }
  page();
  section("3. Resumen General", 36);
  const findings = [
    `${model.total.measurements} mediciones de ${model.lampCount} lámparas; ${model.total.valid} muestras válidas para promedios y ${model.invalid} excluidas por sensor no válido.`,
    model.total.tests
      ? `${model.total.pass} de ${model.total.tests} resultados recibidos informan PASS (${format(model.total.passRate)}%). Se conservan los criterios originales del firmware.`
      : "No se recibieron resultados de pruebas. La ausencia de resultados no equivale a una prueba aprobada.",
    model.best
      ? `Mayor tasa observada: ${model.best.name} ${model.best.year} (${format(model.best.passRate)}%). Menor: ${model.worst.name} ${model.worst.year} (${format(model.worst.passRate)}%). Solo se comparan meses con reportes.`
      : "No hay resultados suficientes para comparar el desempeño entre meses.",
  ];
  const next =
    model.invalid || model.unknownProfiles
      ? "Revisar los registros con sensores no válidos o perfiles no confirmados y contrastar los resultados con la batería instalada. Consultar el detalle por lámpara antes de interpretar promedios de una flota con tensiones distintas."
      : "Mantener el seguimiento de la instalación y revisar los resultados no PASS recibidos. Este reporte no sustituye una comprobación de autonomía o iluminación.";
  const cardText =
    "Hallazgos principales\n" +
    findings.join("\n\n") +
    "\n\nConclusiones y próximos pasos\n" +
    next;
  doc.font("Body").fontSize(9);
  const cardH =
    doc.heightOfString(cardText, { width: W - 24, lineGap: 3 }) + 24;
  rect(X, 61, W, cardH, C.light);
  doc.save().rect(X, 61, 3, cardH).fill(C.green).restore();
  text(cardText, X + 12, 73, W - 24, 9, false, C.ink, { lineGap: 3 });
  let y = 61 + cardH + 24;
  function paragraph(title, body) {
    doc.font("Body").fontSize(8);
    const height = doc.heightOfString(body, { width: W, lineGap: 2 }) + 36;
    if (y + height > 765) {
      page();
      y = 38;
    }
    text(title, X, y, W, 10, true, C.green);
    text(body, X, y + 18, W, 8, false, C.ink, { lineGap: 2 });
    y += height;
  }
  paragraph(
    "Alcance y emisión",
    `Período filtrado: ${stamp(model.from)} a ${stamp(model.to)} (America/Lima).\nÁrea: ${model.scope}.\nResponsable de emisión: ${model.preparedBy}. Código: ${code}.`,
  );
  paragraph(
    "Criterios de lectura",
    `Los promedios se ponderan por cantidad de muestras válidas, no por lámpara ni por tiempo. El rango filtrado puede cubrir solo parte de un mes o año. La variación compara la tasa PASS con el mes calendario inmediatamente anterior; si ese mes no tiene datos se indica "-". No se configuró una meta de cumplimiento.\n${model.receptionOnly} registros usan fecha de recepción por no disponer de una hora de medición fiable. ${model.unknownProfiles} mediciones no tienen un perfil confirmado en su instantánea. El porcentaje de tensión del firmware no mide capacidad ni autonomía.`,
  );
  paragraph(
    "Trazabilidad y archivos de respaldo",
    "Los anexos conservan ubicación, revisión y perfil registrados al recibir los datos. Los cambios posteriores no reinterpretan resultados anteriores. Los datos retransmitidos no permiten inventar una ubicación de origen distinta de la registrada. Los CSV de mediciones y pruebas ofrecen el detalle completo con los mismos filtros.",
  );
  if (model.groups.length || tests.length) {
    page();
    let cursor = 38;
    function annexTitle(label) {
      text(label, X, cursor, W, 12, true, C.green);
      line(X, cursor + 19, W, C.lime);
      cursor += 29;
    }
    function table(columns, rows) {
      function header() {
        let xx = X;
        for (const c of columns) {
          rect(xx, cursor, c.width, 27, C.table, C.table);
          text(c.label, xx + 5, cursor + 5, c.width - 10, 7, true, C.white);
          xx += c.width;
        }
        cursor += 27;
      }
      header();
      rows.forEach((row, index) => {
        const values = row.map((v) => String(v));
        let height = 24;
        for (let i = 0; i < columns.length; i++) {
          doc.font("Body").fontSize(7);
          height = Math.max(
            height,
            doc.heightOfString(values[i], {
              width: columns[i].width - 10,
              lineGap: 2,
            }) + 12,
          );
        }
        if (cursor + height > 768) {
          page();
          cursor = 38;
          header();
        }
        let xx = X;
        values.forEach((v, i) => {
          rect(
            xx,
            cursor,
            columns[i].width,
            height,
            index % 2 ? C.light : C.white,
          );
          text(v, xx + 5, cursor + 6, columns[i].width - 10, 7, false, C.ink, {
            lineGap: 2,
          });
          xx += columns[i].width;
        });
        cursor += height;
      });
      cursor += 25;
    }
    if (model.groups.length) {
      annexTitle("Anexo A. Mediciones por lámpara y configuración");
      table(
        [
          { label: "Lámpara / gateway", width: 112 },
          { label: "Ubicación registrada", width: 142 },
          { label: "Muestras / alertas", width: 63 },
          { label: "Voltaje min./máx. (V)", width: 72 },
          { label: "Revisión / perfil", width: 104 },
        ],
        model.groups.map((g) => [
          `${g.row.name || g.row.device_id}\n${g.row.device_id}\n${g.row.gateway_id}`,
          place(g.row),
          `${g.count} / ${g.alerts}\nVálidas: ${g.valid}`,
          `${g.min === null ? "-" : format(g.min / 1000, 3)} / ${g.max === null ? "-" : format(g.max / 1000, 3)}`,
          profile(g.row),
        ]),
      );
    }
    if (tests.length) {
      if (cursor > 680) {
        page();
        cursor = 38;
      }
      annexTitle("Anexo B. Resultados de pruebas recibidos");
      table(
        [
          { label: "Lámpara / fecha / origen", width: 131 },
          { label: "Resultado original", width: 91 },
          { label: "Duración / V mín.", width: 66 },
          { label: "Ubicación registrada", width: 112 },
          { label: "Revisión / perfil", width: 93 },
        ],
        tests.map((r) => [
          `${r.name || r.device_id}\n${r.device_id} / ${r.gateway_id}\n${stamp(reportDate(r))}\n${r.measured_at ? "Medición" : "Recepción"}`,
          `${r.data.result}\nPrueba: ${r.data.test_id}`,
          `${r.data.duration_s} s\n${format(r.data.voltage_min_mv / 1000, 3)} V`,
          place(r),
          profile(r),
        ]),
      );
    }
  }
  const range = doc.bufferedPageRange();
  for (let i = range.start; i < range.start + range.count; i++) {
    doc.switchToPage(i);
    line(X, 783, W, "#d8e0e8");
    text(
      "Firefly · Reporte de Desempeño Mensual",
      X,
      796,
      W - 120,
      7.3,
      false,
      C.muted,
    );
    text(
      `Página ${i + 1} de ${range.count}`,
      RIGHT - 110,
      796,
      110,
      7.3,
      false,
      C.muted,
      { align: "right" },
    );
  }
  doc.end();
  return done;
}
