/*
 * SingleThread - Task-Centric Wayland Compositor
 * theme.h - Shared theme system for shell components
 *
 * Provides a consistent look across panel, launcher, switcher,
 * and notifications. Supports multiple built-in themes and
 * user customization via config.
 */
#ifndef STW_THEME_H
#define STW_THEME_H

/* ─── Theme definition ─────────────────────────────────────────── */

typedef struct {
	const char *name;

	/* Base colors */
	const char *bg_primary;      /* Main background */
	const char *bg_secondary;    /* Card/surface background */
	const char *bg_tertiary;     /* Input fields, elevated surfaces */
	const char *bg_overlay;      /* Overlay/backdrop */

	/* Text colors */
	const char *fg_primary;      /* Main text */
	const char *fg_secondary;    /* Subdued text */
	const char *fg_muted;        /* Very subtle text */
	const char *fg_on_accent;    /* Text on accent backgrounds */

	/* Accent colors */
	const char *accent;          /* Primary accent */
	const char *accent_dim;      /* Dimmed accent */
	const char *accent_glow;     /* Glow/highlight */

	/* Semantic colors */
	const char *success;
	const char *warning;
	const char *error;
	const char *info;

	/* Border colors */
	const char *border;          /* Default border */
	const char *border_accent;   /* Accent border */
	const char *border_subtle;   /* Very subtle border */

	/* Typography */
	const char *font_sans;       /* UI font */
	const char *font_mono;       /* Monospace font */

	/* Metrics */
	const char *radius_sm;       /* Small radius */
	const char *radius_md;       /* Medium radius */
	const char *radius_lg;       /* Large radius */
} StwTheme;

/* ─── Built-in themes ──────────────────────────────────────────── */

/* Tokyo Night (default dark theme) */
static const StwTheme THEME_TOKYO_NIGHT = {
	.name = "tokyo-night",

	.bg_primary    = "rgba(22, 22, 30, 0.92)",
	.bg_secondary  = "rgba(26, 27, 38, 0.97)",
	.bg_tertiary   = "rgba(36, 40, 59, 0.8)",
	.bg_overlay    = "rgba(15, 15, 22, 0.75)",

	.fg_primary    = "#c0caf5",
	.fg_secondary  = "#a9b1d6",
	.fg_muted      = "rgba(86, 95, 137, 0.6)",
	.fg_on_accent  = "#1a1b26",

	.accent        = "#7aa2f7",
	.accent_dim    = "rgba(122, 162, 247, 0.25)",
	.accent_glow   = "rgba(122, 162, 247, 0.4)",

	.success       = "#9ece6a",
	.warning       = "#e0af68",
	.error         = "#f7768e",
	.info          = "#7dcfff",

	.border        = "rgba(86, 95, 137, 0.25)",
	.border_accent = "rgba(122, 162, 247, 0.3)",
	.border_subtle = "rgba(86, 95, 137, 0.15)",

	.font_sans     = "'Inter', 'Cantarell', 'Noto Sans', sans-serif",
	.font_mono     = "'JetBrains Mono', 'Fira Code', monospace",

	.radius_sm     = "6px",
	.radius_md     = "10px",
	.radius_lg     = "16px",
};

/* Catppuccin Mocha */
static const StwTheme THEME_CATPPUCCIN = {
	.name = "catppuccin",

	.bg_primary    = "rgba(30, 30, 46, 0.92)",
	.bg_secondary  = "rgba(36, 36, 54, 0.97)",
	.bg_tertiary   = "rgba(49, 50, 68, 0.8)",
	.bg_overlay    = "rgba(17, 17, 27, 0.75)",

	.fg_primary    = "#cdd6f4",
	.fg_secondary  = "#bac2de",
	.fg_muted      = "rgba(108, 112, 134, 0.6)",
	.fg_on_accent  = "#1e1e2e",

	.accent        = "#89b4fa",
	.accent_dim    = "rgba(137, 180, 250, 0.25)",
	.accent_glow   = "rgba(137, 180, 250, 0.4)",

	.success       = "#a6e3a1",
	.warning       = "#f9e2af",
	.error         = "#f38ba8",
	.info          = "#89dceb",

	.border        = "rgba(108, 112, 134, 0.25)",
	.border_accent = "rgba(137, 180, 250, 0.3)",
	.border_subtle = "rgba(108, 112, 134, 0.15)",

	.font_sans     = "'Inter', 'Cantarell', 'Noto Sans', sans-serif",
	.font_mono     = "'JetBrains Mono', 'Fira Code', monospace",

	.radius_sm     = "6px",
	.radius_md     = "10px",
	.radius_lg     = "16px",
};

/* Nord */
static const StwTheme THEME_NORD = {
	.name = "nord",

	.bg_primary    = "rgba(46, 52, 64, 0.92)",
	.bg_secondary  = "rgba(59, 66, 82, 0.97)",
	.bg_tertiary   = "rgba(67, 76, 94, 0.8)",
	.bg_overlay    = "rgba(36, 40, 50, 0.75)",

	.fg_primary    = "#eceff4",
	.fg_secondary  = "#d8dee9",
	.fg_muted      = "rgba(129, 161, 193, 0.5)",
	.fg_on_accent  = "#2e3440",

	.accent        = "#88c0d0",
	.accent_dim    = "rgba(136, 192, 208, 0.25)",
	.accent_glow   = "rgba(136, 192, 208, 0.4)",

	.success       = "#a3be8c",
	.warning       = "#ebcb8b",
	.error         = "#bf616a",
	.info          = "#81a1c1",

	.border        = "rgba(129, 161, 193, 0.2)",
	.border_accent = "rgba(136, 192, 208, 0.3)",
	.border_subtle = "rgba(129, 161, 193, 0.1)",

	.font_sans     = "'Inter', 'Cantarell', 'Noto Sans', sans-serif",
	.font_mono     = "'JetBrains Mono', 'Fira Code', monospace",

	.radius_sm     = "4px",
	.radius_md     = "8px",
	.radius_lg     = "12px",
};

/* Gruvbox Dark */
static const StwTheme THEME_GRUVBOX = {
	.name = "gruvbox",

	.bg_primary    = "rgba(40, 40, 40, 0.92)",
	.bg_secondary  = "rgba(50, 48, 47, 0.97)",
	.bg_tertiary   = "rgba(60, 56, 54, 0.8)",
	.bg_overlay    = "rgba(29, 32, 33, 0.75)",

	.fg_primary    = "#ebdbb2",
	.fg_secondary  = "#d5c4a1",
	.fg_muted      = "rgba(168, 153, 132, 0.5)",
	.fg_on_accent  = "#282828",

	.accent        = "#83a598",
	.accent_dim    = "rgba(131, 165, 152, 0.25)",
	.accent_glow   = "rgba(131, 165, 152, 0.4)",

	.success       = "#b8bb26",
	.warning       = "#fabd2f",
	.error         = "#fb4934",
	.info          = "#83a598",

	.border        = "rgba(168, 153, 132, 0.2)",
	.border_accent = "rgba(131, 165, 152, 0.3)",
	.border_subtle = "rgba(168, 153, 132, 0.1)",

	.font_sans     = "'Inter', 'Cantarell', 'Noto Sans', sans-serif",
	.font_mono     = "'JetBrains Mono', 'Fira Code', monospace",

	.radius_sm     = "4px",
	.radius_md     = "8px",
	.radius_lg     = "12px",
};

/* ─── Theme resolution ─────────────────────────────────────────── */

static inline const StwTheme *stw_theme_get(const char *name) {
	if (!name) return &THEME_TOKYO_NIGHT;
	if (strcmp(name, "catppuccin") == 0) return &THEME_CATPPUCCIN;
	if (strcmp(name, "nord") == 0) return &THEME_NORD;
	if (strcmp(name, "gruvbox") == 0) return &THEME_GRUVBOX;
	return &THEME_TOKYO_NIGHT;
}

#endif /* STW_THEME_H */
