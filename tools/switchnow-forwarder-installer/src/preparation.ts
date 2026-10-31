// Keep heavy synchronous package generation behind decoded artwork and two
// complete animation turns. A fire-and-forget decode races the WASM task.
export async function prepareVisibleInstaller(
  loadBackground: () => Promise<void>,
  nextFrame: () => Promise<void>,
): Promise<void> {
  try { await loadBackground(); } catch { /* Artwork failure must not block installation. */ }
  await nextFrame();
  await nextFrame();
}
