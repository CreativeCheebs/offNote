// Core AFFiNE append logic, shared by the CLI (affine-append.js, reads a job
// from stdin) and the HTTP sidecar (server.js, reads a job from a POST body)
// so the Yjs/socket.io protocol logic lives in exactly one place.
import { signIn, connect, joinWorkspace, loadDoc, pushDocUpdate, Y } from './lib.js';

const log = (...a) => console.error('[affine]', ...a);

const ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
const blockId = () =>
  Array.from({ length: 10 }, () => ALPHABET[Math.floor(Math.random() * ALPHABET.length)]).join('');

function decode(ack) {
  const doc = new Y.Doc();
  if (ack?.data?.missing) Y.applyUpdate(doc, Buffer.from(ack.data.missing, 'base64'));
  return doc;
}

// The user's LOCAL calendar day (YYYY-MM-DD). Journals are keyed by local date
// in the Affine client, so this must NOT use UTC (that would misfile near
// midnight). Deliberately distinct from the DB's UTC created_at instant.
function localToday() {
  const d = new Date();
  const p = (n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}`;
}

const NANO = 'useandom26T198340PX75pxJACKVERYMINDBUSHWOLFGQZbfghjklqvwyzrict';
const nanoid = (len = 21) =>
  Array.from({ length: len }, () => NANO[Math.floor(Math.random() * NANO.length)]).join('');

// The set of page ids that are still LIVE (present in the workspace root's
// meta.pages). Deleting a page removes its meta.pages entry but leaves its
// docProperties marker behind, so meta.pages is the source of truth for
// existence.
async function livePageIds(socket, ws) {
  const root = decode(await loadDoc(socket, ws, ws));
  const pages = root.getMap('meta').get('pages');
  const ids = new Set();
  if (pages) {
    for (const entry of pages.toArray()) {
      const id = entry?.get ? entry.get('id') : entry?.id;
      if (id) ids.add(id);
    }
  }
  return ids;
}

// Find the LIVE page whose docProperties mark it as the journal for `date`.
// Read-only; returns the pageId or null. A trashed page (marker still present
// but no longer in meta.pages) is ignored, so a fresh journal gets created.
async function findJournalPage(socket, ws, date) {
  const live = await livePageIds(socket, ws);
  const props = decode(await loadDoc(socket, ws, `db$${ws}$docProperties`));
  for (const pageId of props.share.keys()) {
    if (!live.has(pageId)) continue;
    let marker;
    try {
      marker = props.getMap(pageId).toJSON();
    } catch {
      continue;
    }
    if (marker && marker.journal === date) return pageId;
  }
  return null;
}

// Create the journal page for `date` from scratch: (1) the page doc with the
// standard page/surface/note block tree, (2) a root meta.pages entry, and
// (3) the docProperties journal marker (written LAST, so a find always sees a
// fully-formed journal). Returns the new pageId.
async function createJournalPage(socket, ws, date, userId) {
  const pageId = nanoid();
  const now = Date.now();
  const pageBlockId = nanoid(10);
  const surfaceId = nanoid(10);
  const noteId = nanoid(10);

  const pdoc = new Y.Doc();
  const blocks = pdoc.getMap('blocks');
  pdoc.transact(() => {
    const page = new Y.Map();
    page.set('sys:id', pageBlockId);
    page.set('sys:flavour', 'affine:page');
    page.set('sys:version', 2);
    const pageChildren = new Y.Array();
    pageChildren.push([surfaceId, noteId]);
    page.set('sys:children', pageChildren);
    page.set('prop:title', new Y.Text(date));
    blocks.set(pageBlockId, page);

    const surface = new Y.Map();
    surface.set('sys:id', surfaceId);
    surface.set('sys:flavour', 'affine:surface');
    surface.set('sys:version', 5);
    surface.set('sys:children', new Y.Array());
    surface.set('prop:elements', new Y.Map());
    blocks.set(surfaceId, surface);

    const note = new Y.Map();
    note.set('sys:id', noteId);
    note.set('sys:flavour', 'affine:note');
    note.set('sys:version', 1);
    note.set('sys:children', new Y.Array());
    note.set('prop:xywh', '[0,0,498,92]');
    const bg = new Y.Map();
    bg.set('dark', '#252525');
    bg.set('light', '#ffffff');
    note.set('prop:background', bg);
    note.set('prop:index', 'a0');
    note.set('prop:lockedBySelf', false);
    note.set('prop:hidden', false);
    note.set('prop:displayMode', 'both');
    const edgeless = new Y.Map();
    const style = new Y.Map();
    style.set('borderRadius', 8);
    style.set('borderSize', 4);
    style.set('borderStyle', 'none');
    style.set('shadowType', '--affine-note-shadow-box');
    edgeless.set('style', style);
    note.set('prop:edgeless', edgeless);
    blocks.set(noteId, note);
  }, 'offnote-create-journal');
  const pushed = await pushDocUpdate(socket, ws, pageId, Y.encodeStateAsUpdate(pdoc));
  if (pushed?.error) throw new Error('create page doc failed: ' + JSON.stringify(pushed.error));

  const root = decode(await loadDoc(socket, ws, ws));
  const meta = root.getMap('meta');
  const beforeRoot = Y.encodeStateVector(root);
  root.transact(() => {
    let pages = meta.get('pages');
    if (!pages) {
      pages = new Y.Array();
      meta.set('pages', pages);
    }
    const entry = new Y.Map();
    entry.set('id', pageId);
    entry.set('title', date);
    entry.set('createDate', now);
    entry.set('tags', new Y.Array());
    entry.set('updatedDate', now);
    pages.push([entry]);
  }, 'offnote-create-journal');
  const rootPush = await pushDocUpdate(socket, ws, ws, Y.encodeStateAsUpdate(root, beforeRoot));
  if (rootPush?.error) throw new Error('register meta.pages failed: ' + JSON.stringify(rootPush.error));

  const propsId = `db$${ws}$docProperties`;
  const props = decode(await loadDoc(socket, ws, propsId));
  const beforeProps = Y.encodeStateVector(props);
  props.transact(() => {
    const m = props.getMap(pageId);
    m.set('id', pageId);
    m.set('primaryMode', 'page');
    m.set('journal', date);
    m.set('createdBy', userId);
    m.set('updatedBy', userId);
  }, 'offnote-create-journal');
  const propsPush = await pushDocUpdate(socket, ws, propsId, Y.encodeStateAsUpdate(props, beforeProps));
  if (propsPush?.error) throw new Error('set journal marker failed: ' + JSON.stringify(propsPush.error));

  log('created journal page', pageId, 'for', date);
  return pageId;
}

/**
 * Append one or more paragraphs to a page (explicit pageId, or journal:true
 * for "today's journal, creating it if needed"). Job shape:
 *   { base?, email, password, workspaceId, pageId?, journal?, date?, create?, texts: [...] }
 * Returns { pageId } on a verified round-trip; throws on any failure.
 */
export async function appendNote(job) {
  const { email, password, workspaceId } = job;
  const texts = (job.texts || []).filter((t) => typeof t === 'string' && t.trim().length);
  if (!email || !password || !workspaceId) {
    throw new Error('job requires email, password, workspaceId');
  }
  if (!job.pageId && !job.journal) {
    throw new Error('job requires a target: set pageId, or journal:true');
  }
  if (texts.length === 0) throw new Error('job has no non-empty texts');

  if (job.base) process.env.AFFINE_BASE = job.base;

  const session = await signIn(email, password);
  log('signed in as', session.user.email);
  const socket = await connect(session);
  try {
    await joinWorkspace(socket, workspaceId);

    let pageId = job.pageId;
    if (!pageId && job.journal) {
      const date = job.date || localToday();
      pageId = await findJournalPage(socket, workspaceId, date);
      if (pageId) {
        log('resolved journal for', date, '->', pageId);
      } else if (job.create === false) {
        throw new Error(`no journal page for ${date} (create disabled)`);
      } else {
        pageId = await createJournalPage(socket, workspaceId, date, session.user.id);
      }
    }

    const ack = await loadDoc(socket, workspaceId, pageId);
    if (ack.error) throw new Error('load-doc failed: ' + JSON.stringify(ack.error));
    const doc = decode(ack);
    const blocks = doc.getMap('blocks');

    let noteId = null;
    for (const [id, b] of blocks.entries()) {
      if (b.get('sys:flavour') === 'affine:note') {
        noteId = id;
        break;
      }
    }
    if (!noteId) throw new Error('no affine:note block found in page');
    const note = blocks.get(noteId);
    const children = note.get('sys:children');

    const beforeSV = Y.encodeStateVector(doc);
    const now = Date.now();
    const newIds = [];
    doc.transact(() => {
      for (const text of texts) {
        const id = blockId();
        newIds.push(id);
        const para = new Y.Map();
        para.set('sys:id', id);
        para.set('sys:flavour', 'affine:paragraph');
        para.set('sys:version', 1);
        para.set('sys:children', new Y.Array());
        para.set('prop:type', 'text');
        para.set('prop:text', new Y.Text(text));
        para.set('prop:collapsed', false);
        para.set('prop:meta:createdAt', now);
        para.set('prop:meta:createdBy', session.user.id);
        para.set('prop:meta:updatedAt', now);
        para.set('prop:meta:updatedBy', session.user.id);
        blocks.set(id, para);
        children.push([id]);
      }
    }, 'offnote-affine-connector');

    const update = Y.encodeStateAsUpdate(doc, beforeSV);
    log(`pushing ${newIds.length} paragraph(s) (${update.byteLength} bytes)`);
    const pushAck = await pushDocUpdate(socket, workspaceId, pageId, update);
    if (pushAck?.error) throw new Error('push failed: ' + JSON.stringify(pushAck.error));

    const verifyDoc = decode(await loadDoc(socket, workspaceId, pageId));
    const vchildren = verifyDoc.getMap('blocks').get(noteId).get('sys:children').toArray();
    const ok = newIds.every((id) => vchildren.includes(id));
    if (!ok) throw new Error('verify failed: new entries not found on server');
    log('OK: appended and verified', newIds.length, 'paragraph(s)');
    return { pageId };
  } finally {
    socket.close();
  }
}
