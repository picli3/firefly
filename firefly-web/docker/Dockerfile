FROM node:22-bookworm-slim AS build
WORKDIR /app
COPY package*.json ./
RUN npm ci
COPY . .
RUN npm run build
FROM node:22-bookworm-slim
WORKDIR /app
ENV NODE_ENV=production
COPY --from=build /app/.output ./.output
COPY --from=build /app/node_modules ./node_modules
COPY --from=build /app/services ./services
# shared/ lo usan también los servicios (planificación de pruebas en el worker).
COPY --from=build /app/shared ./shared
COPY --from=build /app/scripts ./scripts
COPY --from=build /app/package.json ./package.json
CMD ["node", ".output/server/index.mjs"]
