/** @type {import('tailwindcss').Config} */
export default {
    content: [
        "./index.html",
        "./src/**/*.{js,ts,jsx,tsx}",
    ],
    theme: {
        extend: {
            colors: {
                'bg-dark': '#0f172a',
                'bg-card': '#1e293b',
                'text-primary': '#f1f5f9',
                'text-secondary': '#94a3b8',
                'accent': '#38bdf8',
                'accent-glow': 'rgba(56, 189, 248, 0.2)',
                'success': '#4ade80',
                'danger': '#f87171',
                'border': '#334155',
            }
        },
    },
    plugins: [],
}
