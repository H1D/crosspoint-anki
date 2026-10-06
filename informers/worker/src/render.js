import { Canvas } from "./canvas.js";

// Draws an informer and encodes it. Query `depth=2` asks for 4-level gray;
// anything else gets black and white.
export async function renderBmp(informer, params, env = {}) {
  const canvas = new Canvas({ depth: params.get("depth") === "2" ? 2 : 1 });
  await informer.render(canvas, params, env);
  return canvas.toBmp();
}
