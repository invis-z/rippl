import assert from "node:assert/strict";
import { drawingEditor } from "./drawing_editor.js";
import { FRAME_BUDGET_MS, analyseDrawing, element } from "./drawing.js";

const box = (x, y, w, h) => ({ x, y, w, h });
const stub = () => ({});

function editorFor(drawing, shape = "wide") {
  const state = { picked: null, zoom: 1 };
  const said = [];
  const editor = drawingEditor({
    state, el: stub, field: stub, group: stub, chips: stub, range: stub, toggle: stub, choiceColour: stub, icon: stub,
    drawing: () => drawing.current, shape: () => shape, render: () => {}, apply: () => {}, changed: () => {}, fits: () => true,
    say: (text) => said.push(text), remember: () => {},
  });
  return { editor, state, said };
}

function click(editor, x, y, add = false) {
  editor.down({ x, y }, { add });
  editor.up();
}

function drag(editor, from, to, free = false) {
  editor.down(from);
  editor.move(to, { free });
  editor.up();
}

const key = (name, extra = {}) => ({ key: name, ctrlKey: false, metaKey: false, shiftKey: false, ...extra });

const background = element("box", box(0, 0, 320, 146), box(0, 0, 170, 280), { name: "Background", locked: true });
const left = element("box", box(20, 20, 40, 30), box(10, 10, 40, 30));
const middle = element("box", box(100, 30, 20, 20), box(60, 60, 20, 20));
const right = element("box", box(200, 50, 60, 10), box(100, 100, 60, 10));
const menu = element("menu", box(0, 0, 150, 146), box(0, 0, 170, 280));
const home = { current: { version: 2, screen: "home", elements: [background, menu, left, middle, right] } };
const { editor, state, said } = editorFor(home);

click(editor, 5, 160);
assert.equal(state.picked, menu.id, "a locked background can't be picked on the screen");
click(editor, 30, 50);
assert.equal(state.picked, left.id);
assert.equal(editor.key(key("a", { ctrlKey: true })), true);
assert.deepEqual([state.picked, ...state.also.ids], [menu.id, left.id, middle.id, right.id], "Ctrl+A picks every unlocked part");
editor.key(key("escape"));
assert.equal(state.picked, null);
click(editor, 30, 50);
click(editor, 105, 60, true);
click(editor, 210, 75, true);
assert.deepEqual([state.picked, ...state.also.ids], [left.id, middle.id, right.id], "Shift-click adds parts");

drag(editor, { x: 30, y: 50 }, { x: 28.6, y: 50 });
assert.deepEqual([left.at.wide.x, middle.at.wide.x, right.at.wide.x], [19, 99, 199], "several parts move as one");
drag(editor, { x: 30, y: 50 }, { x: 12.5, y: 50 });
assert.equal(left.at.wide.x, 0, "the left edge snaps to the screen's edge");
assert.deepEqual([middle.at.wide.x, right.at.wide.x], [80, 180], "the others move with it");
drag(editor, { x: 10, y: 50 }, { x: 12, y: 50 }, true);
assert.equal(left.at.wide.x, 2, "Alt places freely");

editor.key(key("g", { ctrlKey: true }));
const grouped = home.current.elements.find((item) => item.kind === "group");
assert.ok(grouped, "Ctrl+G groups the picked parts");
assert.deepEqual(grouped.at.wide, { x: 2, y: 20, w: 240, h: 40 });
assert.deepEqual(grouped.children.map((item) => [item.at.wide.x, item.at.wide.y]), [[0, 0], [80, 10], [180, 30]], "parts keep their place on the screen");
assert.deepEqual(grouped.at.tall, { x: 10, y: 10, w: 150, h: 100 });
editor.key(key("g", { ctrlKey: true, shiftKey: true }));
assert.equal(home.current.elements.some((item) => item.kind === "group"), false, "Ctrl+Shift+G ungroups");
assert.deepEqual([left.at.wide.x, middle.at.wide.y, right.at.wide.x], [2, 30, 182]);

const sorted = [left, middle, right];
editor.key(key("escape"));
click(editor, 10, 50);
click(editor, 85, 60, true);
click(editor, 190, 75, true);
const before = sorted.map((item) => item.at.wide.x);
assert.deepEqual(before, [2, 82, 182]);

const playing = { current: { version: 2, screen: "playing", elements: [] } };
assert.equal(editor.key(key("c", { ctrlKey: true })), true);
assert.match(said.at(-1), /Copied 3 parts/);
const other = editorFor(playing);
assert.equal(other.editor.key(key("v", { ctrlKey: true })), true);
assert.equal(playing.current.elements.length, 3, "parts paste onto Now Playing");
assert.deepEqual(playing.current.elements.map((item) => item.at.wide.x), [2, 82, 182], "in the same place on another screen");
assert.ok(playing.current.elements.every((item) => !sorted.includes(item) && !sorted.some((one) => one.id === item.id)), "as new parts");

click(editor, 140, 100);
assert.equal(state.picked, menu.id);
editor.key(key("c", { ctrlKey: true }));
other.editor.key(key("v", { ctrlKey: true }));
assert.equal(playing.current.elements.length, 3, "the menu isn't pasted on Now Playing");
assert.match(other.said.at(-1), /menu only goes on Home/);

const tileIcon = element("symbol", box(280, 120, 24, 24), box(140, 250, 24, 24), { mark: "item" });
home.current.elements.push(tileIcon);
editor.key(key("escape"));
click(editor, 290, 155);
assert.equal(state.picked, tileIcon.id);
editor.key(key("c", { ctrlKey: true }));
other.editor.key(key("v", { ctrlKey: true }));
assert.equal(playing.current.elements.at(-1).mark, "state", "a tile icon pasted on Now Playing becomes a play mark");
home.current.elements.pop();

right.locked = true;
editor.key(key("escape"));
click(editor, 200, 75);
assert.notEqual(state.picked, right.id, "a locked part isn't picked on the screen");

right.locked = false;
menu.locked = true;
editor.down({ x: 300, y: 160 });
editor.move({ x: 90, y: 40 });
editor.up();
assert.deepEqual([state.picked, state.also?.ids], [middle.id, [right.id]], "dragging across locked parts picks what it touches");

click(editor, 100, 60);
editor.key(key("Delete"));
assert.equal(home.current.elements.includes(middle) || home.current.elements.includes(right), false, "Delete removes the picked parts");

const one = element("box", box(10, 10, 20, 20), null);
const two = element("box", box(50, 40, 40, 10), null);
const three = element("box", box(200, 20, 10, 30), null);
const lined = { current: { version: 2, screen: "playing", elements: [one, two, three] } };
const tidy = editorFor(lined);
click(tidy.editor, 15, 40);
click(tidy.editor, 60, 70, true);
click(tidy.editor, 205, 50, true);
tidy.editor.lineUp("top");
assert.deepEqual([one, two, three].map((item) => item.at.wide.y), [10, 10, 10], "line up the tops of several parts");
tidy.editor.spread("across");
assert.deepEqual([one, two, three].map((item) => item.at.wide.x), [10, 95, 200], "space them evenly across");
tidy.editor.key(key("escape"));
click(tidy.editor, 15, 40);
tidy.editor.lineUp("centre");
assert.equal(one.at.wide.x, 150, "one part lines up with the middle of the screen");
tidy.editor.lineUp("bottom");
assert.equal(one.at.wide.y, 126, "and with its bottom");

tidy.editor.key(key("escape"));
assert.deepEqual(tidy.editor.settings("playing"), [], "with nothing picked there are no part settings to show");
assert.deepEqual(["adding", "parts", "settings"].filter((name) => typeof tidy.editor[name] !== "function"), [], "the page gets the add menu, the parts list and the settings apart");

click(tidy.editor, 155, 155);
assert.deepEqual(tidy.editor.frame(), { x: 150, y: 150, w: 20, h: 20 }, "the picked part's place on the screen, for the bar beside it");
assert.deepEqual(tidy.editor.picks().map((item) => item.id), [one.id]);
tidy.editor.down({ x: 155, y: 155 });
assert.equal(tidy.editor.dragging(), true);
tidy.editor.up();
assert.equal(tidy.editor.dragging(), false);
tidy.editor.key(key("escape"));
assert.equal(tidy.editor.frame(), null);

const shelfHome = { current: { version: 2, screen: "home", elements: [
  element("box", box(0, 0, 320, 146), box(0, 0, 170, 280), { locked: true }),
  element("menu", box(0, 0, 176, 146), box(0, 0, 170, 152)),
] } };
const shelf = editorFor(shelfHome);
shelf.editor.add("tiles", "home");
assert.equal(shelfHome.current.elements.some((item) => item.kind === "menu"), false, "tiles take the list's place");
const tileGroups = shelfHome.current.elements.filter((item) => item.repeat?.count === "items");
assert.deepEqual(tileGroups.map((item) => item.at.wide ?? item.at.tall), [box(0, 0, 176, 146), box(0, 0, 170, 152)], "in the same spot, both ways up");
assert.equal(shelf.state.picked, tileGroups[0].id);
shelf.editor.add("tiles", "home");
assert.match(shelf.said.at(-1), /already on this screen/);
shelf.editor.add("menu", "home");
assert.deepEqual(shelfHome.current.elements.map((item) => item.kind), ["box", "menu"], "and the list takes the tiles' place");
assert.deepEqual(shelfHome.current.elements[1].at, { wide: box(0, 0, 176, 146), tall: box(0, 0, 170, 152) });
shelf.editor.add("title", "home");
shelf.editor.add("title", "home");
const titles = shelfHome.current.elements.filter((item) => item.source === "song");
assert.deepEqual(titles.map((item) => item.at.wide.x), [70, 76], "a second part of the same kind lands beside the first, not on it");

const bg = element("box", box(0, 0, 320, 146), box(0, 0, 170, 280), { name: "Backdrop" });
const customStatic = element("text", box(10, 10, 80, 18), box(10, 10, 80, 18), { source: "custom", custom: "HI-FI", scroll: false });
const playMark = element("symbol", box(10, 30, 18, 18), box(10, 30, 18, 18), { mark: "play" });
const songTitle = element("text", box(10, 50, 180, 20), box(10, 50, 150, 20), { source: "song", scroll: true });
const ringAfter = element("circle", box(200, 20, 90, 90), box(40, 120, 90, 90), { thickness: 4 });
const badOrder = { version: 2, screen: "playing", elements: [bg, customStatic, playMark, songTitle, ringAfter] };
const badReport = analyseDrawing(badOrder, "wide");
assert.equal(badReport.caches, true);
assert.equal(badReport.parts.get(bg.id).status, "cached");
assert.equal(badReport.parts.get(customStatic.id).status, "cached");
assert.equal(badReport.parts.get(playMark.id).status, "cached");
assert.equal(badReport.parts.get(songTitle.id).status, "dynamic");
assert.equal(badReport.parts.get(songTitle.id).blocksCache, true, "dynamic part under static part is flagged");
assert.equal(badReport.parts.get(ringAfter.id).status, "blocked", "static part after dynamic part is not cached");
assert.deepEqual(badReport.blockedStatic.map((item) => item.id), [ringAfter.id]);
assert.deepEqual(badReport.blockingDynamic.map((item) => item.id), [songTitle.id]);

const goodOrder = { version: 2, screen: "playing", elements: [bg, customStatic, playMark, ringAfter, songTitle] };
const goodReport = analyseDrawing(goodOrder, "wide");
assert.equal(goodReport.parts.get(ringAfter.id).status, "cached", "moving static part behind moving part caches it");
assert.equal(goodReport.parts.get(songTitle.id).blocksCache, false);
assert.equal(goodReport.blockedStatic.length, 0);
assert.ok(goodReport.estimatedMs < badReport.estimatedMs, "caching the static ring lowers estimated frame cost");
assert.equal(goodReport.overBudget, false);

const scrollingCustom = element("text", box(10, 10, 80, 18), null, { source: "custom", custom: "Scrolling", scroll: true });
const stateMark = element("symbol", box(10, 30, 18, 18), null, { mark: "state" });
const spinningGroup = element("group", box(0, 0, 80, 80), null, {
  repeat: { count: 8, spin: 2 },
  children: [element("line", box(40, 40, 20, 0), null, { thickness: 2 })],
});
const staticGroup = element("group", box(0, 0, 80, 80), null, {
  repeat: { count: 8, spin: 0 },
  children: [element("line", box(40, 40, 20, 0), null, { thickness: 2 })],
});
const groupReport = analyseDrawing({ version: 2, screen: "playing", elements: [staticGroup, scrollingCustom, stateMark, spinningGroup] }, "wide");
assert.equal(groupReport.parts.get(staticGroup.id).status, "cached");
assert.equal(groupReport.parts.get(scrollingCustom.id).status, "dynamic");
assert.equal(groupReport.parts.get(stateMark.id).status, "dynamic");
assert.equal(groupReport.parts.get(spinningGroup.id).status, "dynamic");

const heavySpokes = element("group", box(0, 0, 140, 140), null, {
  repeat: { count: 96, spin: 3 },
  children: [
    element("line", box(70, 70, 60, 0), null, { thickness: 4 }),
    element("circle", box(0, 0, 120, 120), null, { thickness: 6 }),
  ],
});
const heavyReport = analyseDrawing({ version: 2, screen: "playing", elements: [bg, heavySpokes] }, "wide");
assert.ok(heavyReport.estimatedMs > FRAME_BUDGET_MS);
assert.equal(heavyReport.overBudget, true, "heavy dynamic screen triggers over-budget warning");

const homeReport = analyseDrawing({ version: 2, screen: "home", elements: [songTitle, bg] }, "wide");
assert.equal(homeReport.caches, false);
assert.equal(homeReport.parts.get(bg.id).status, "static");
assert.equal(homeReport.blockedStatic.length, 0);

const checkedEditor = editorFor({ current: badOrder });
assert.equal(checkedEditor.editor.analyse().blockedStatic.length, 1);

console.log("ok: drawing tools");
