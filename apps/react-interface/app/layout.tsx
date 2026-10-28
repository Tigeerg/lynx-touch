import { Analytics } from '@vercel/analytics/next'
import type { Metadata, Viewport } from 'next'
import { Inter, JetBrains_Mono } from 'next/font/google'
import { themeBootScript } from '@/lib/cnc/theme'
import './globals.css'

// The saved theme must be on the html element before the first paint. The live effect alone
// paints the default light palette for one frame after a reload. The script itself lives in
// lib/cnc/theme.ts next to the class table, so an empty storage cannot open another theme.
const themeBoot = themeBootScript()

const inter = Inter({ subsets: ['latin', 'cyrillic'], variable: '--font-inter' })
const jetbrains = JetBrains_Mono({ subsets: ['latin', 'cyrillic'], variable: '--font-jetbrains' })

export const metadata: Metadata = {
  title: 'Lynx Touch — ЧПУ контроллер на базе LinuxCNC',
  description:
    'Lynx Touch — панель оператора ЧПУ контроллера на базе LinuxCNC, ориентированная на управление физическими клавишами',
  generator: 'v0.app',
  icons: {
    icon: '/lynx-logo.png',
    apple: '/lynx-logo.png',
  },
}

export const viewport: Viewport = {
  colorScheme: 'light',
  themeColor: '#e6e9ee',
}

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode
}>) {
  // No theme class here: the default theme is "Светлая" (param 2) and it is the base :root
  // palette. A hard-coded theme-minimal painted the panel as "Минимал · светлая" until the
  // theme effect ran, which was a flash of the wrong theme on the first frame.
  return (
    <html lang="ru" className={`${inter.variable} ${jetbrains.variable} bg-background`} suppressHydrationWarning>
      <head>
        <script dangerouslySetInnerHTML={{ __html: themeBoot }} />
      </head>
      <body className="antialiased">
        {children}
        {process.env.NODE_ENV === 'production' && <Analytics />}
      </body>
    </html>
  )
}
