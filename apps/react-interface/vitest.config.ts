// The checks run on the same alias the application compiles with: `@/` is the package root
// (tsconfig `paths`). The UI helpers live under `components/`, so their tests need the alias that
// `lib/` tests only reach through relative imports.
import { fileURLToPath } from 'node:url'
import { defineConfig } from 'vitest/config'

export default defineConfig({
  resolve: {
    alias: { '@': fileURLToPath(new URL('.', import.meta.url)) },
  },
})
