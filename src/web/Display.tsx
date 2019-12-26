import React from "react";
import cx from "classnames";

import nano from "./nano";
import { Matrix } from "../wasm";
import PicoSignal from "../wasm/PicoSignal";

const CSS = {
  root: nano.rule({
    position: "relative",
    backgroundColor: "white",
    padding: "1px",
    flex: "0 auto"
  }),

  draw: nano.rule({
    position: "relative",
    display: "block",
    margin: "-1px"
  }),

  grid: nano.rule({
    position: "absolute",
    top: "0",
    left: "0",
    transformOrigin: "0 0",
    backgroundSize: `8px 8px`,
    zIndex: 0
  })
};

const PADDING = 1;

const TRANSPARENCY_PALETTE = [0, 85, 170, 255];

const WHITE = Uint8Array.of(15, 56, 15, 1);

export default function Display(props: {
  className?: string;
  pixels: () => Matrix<Uint8Array>;
  scale?: number;
  signal?: PicoSignal<any>;
  gridColor?: string;
  blend?: boolean;
}) {
  const pixels = props.pixels();

  const prev = React.useMemo(() => {
    return new Uint8Array(pixels.width * pixels.height);
  }, []);

  const imageData = React.useMemo(() => {
    const imageData = new ImageData(pixels.width, pixels.height);
    for (let i = 0; i < pixels.length; i++) {
      imageData.data.set(WHITE, 4 * i);
    }
    return imageData;
  }, []);

  const drawRef = React.useRef<HTMLCanvasElement>(null);

  const render = React.useCallback(() => {
    if (!drawRef.current) return;

    const ctx = drawRef.current.getContext("2d")!;
    const pixels = props.pixels();
    if (props.blend) {
      for (let i = 0; i < pixels.length; i++) {
        const shade = pixels[i];
        const prevAlpha = prev[i];
        // const newAlpha = TRANSPARENCY_PALETTE[shade];
        const newAlpha = shade * 85;
        imageData.data[4 * i + 3] = (prevAlpha + newAlpha) >> 1;
        prev[i] = newAlpha;
      }
    } else {
      for (let i = 0; i < pixels.length; i++) {
        const shade = pixels[i];
        // const newAlpha = TRANSPARENCY_PALETTE[shade];
        const newAlpha = shade * 85;
        imageData.data[4 * i + 3] = newAlpha;
      }
    }
    ctx.putImageData(imageData, PADDING, PADDING);
  }, []);

  React.useEffect(render, [drawRef.current]);

  React.useEffect(() => {
    if (props.signal) {
      const signal = props.signal;
      signal.add(render);
      return () => signal.remove(render);
    }
  }, [props.signal]);

  const scale = props.scale || 1;
  const width = pixels.width + PADDING * 2;
  const height = pixels.height + PADDING * 2;

  return (
    <div className={cx(CSS.root, props.className)}>
      {props.gridColor && (
        <div
          className={CSS.grid}
          style={{
            width: pixels.width + PADDING * 2,
            height: pixels.height + PADDING * 2,
            transform: `scale(${scale})`,
            backgroundImage: [
              `linear-gradient(to right, ${props.gridColor} 2px, transparent 1px)`,
              `linear-gradient(to bottom, ${props.gridColor} 2px, transparent 1px)`
            ].join(",")
          }}
        />
      )}
      <canvas
        ref={drawRef}
        className={CSS.draw}
        width={width}
        height={height}
        style={{ width: width * scale }}
      />
    </div>
  );
}
