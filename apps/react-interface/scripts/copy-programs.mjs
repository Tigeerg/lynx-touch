// Copies the shared NC test program from the repository into `public/programs`, where the panel
// fetches it on first open. The source file lives in `data/cad-testdata` (the viewer uses it too),
// so the 2.9 MB program is not duplicated in git: the copy is regenerated before dev and build.
import { copyFileSync, existsSync, mkdirSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const scriptsDir = dirname(fileURLToPath(import.meta.url))
const appDir = join(scriptsDir, '..')
const source = join(appDir, '..', '..', 'data', 'cad-testdata', 'Test_model.ngc')
const targetDir = join(appDir, 'public', 'programs')
const target = join(targetDir, 'Test_model.ngc')

if (!existsSync(source)) {
  console.warn(`[programs] ${source} not found — TEST_MODEL will fail to load`)
  process.exit(0)
}

mkdirSync(targetDir, { recursive: true })
copyFileSync(source, target)
console.log('[programs] Test_model.ngc copied to public/programs')
