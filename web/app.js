'use strict';
/*
  myTinySQL - the behaviour of the browser page.

  The page talks to the server with two requests:
    GET  /api/tables   the tables and their columns
    POST /api/sql      runs the SQL sent in the body; the answer is {"results": [...]}

  Every action (search, add, edit, delete, new table) builds ordinary SQL, sends it with POST /api/sql,
  and shows it above the rows, so you can always see which statement did what.

  Text that comes from the server is always put on the page with textContent, never as HTML:
  a value such as "<b>Rossi</b>" is shown exactly as typed and can never change the page.
*/

// ---------------------------------------------------------------- Talking to the server

/** Asks the server for the tables and their columns. */
async function fetchTables() {
  const response = await fetch('/api/tables');
  if (!response.ok) throw new Error(`Cannot load the tables (HTTP ${response.status})`);
  return (await response.json()).tables;
}

/** Sends SQL to the server and returns one result per statement. */
async function runSql(sql) {
  const response = await fetch('/api/sql', {
    method: 'POST',
    headers: { 'Content-Type': 'text/plain; charset=utf-8' },
    body: sql,
  });
  if (!response.ok) throw new Error(`The server answered HTTP ${response.status}`);
  return (await response.json()).results;
}

/** A letter, a digit, or any non-ASCII character such as è: the server's rule for what may surround an apostrophe. */
const isWordChar = (c) => c !== undefined && /[A-Za-z0-9\u0080-\uffff]/.test(c);

/**
 * Writes a value as an SQL string. An apostrophe between two letters stays as it is (d'Asti, L'Aquila):
 * the server reads it as part of the word. Any other quote is doubled: Ca' Foscari -> 'Ca'' Foscari'.
 */
const quote = (value) => "'" + String(value).replace(/'/g, (mark, i, text) =>
  isWordChar(text[i - 1]) && isWordChar(text[i + 1]) ? mark : "''") + "'";

// ---------------------------------------------------------------- Small helpers

/** The element with this id. */
const $ = (id) => document.getElementById(id);

/** Creates an element, sets its properties, and appends the children (strings become plain text). */
function element(tag, properties = {}, ...children) {
  const node = document.createElement(tag);
  Object.assign(node, properties);
  for (const child of children) {
    if (child !== null && child !== undefined) node.append(child);
  }
  return node;
}

/** Shows a short message in the corner; errors stay a little longer. */
function toast(text, kind = '') {
  const node = element('div', { className: `toast ${kind}`, textContent: text });
  $('toasts').append(node);
  setTimeout(() => node.remove(), kind === 'error' ? 6000 : 3000);
}

/** Remembers a small setting in this browser (e.g. the last table); the page also works without it. */
function remember(key, value) {
  try { localStorage.setItem(key, value); } catch (error) { /* storage blocked: not important */ }
}
function recall(key) {
  try { return localStorage.getItem(key); } catch (error) { return null; }
}

// ---------------------------------------------------------------- Settings

/** The text sizes offered in Settings: the base size of the page, in pixels. */
const TEXT_SIZES = { 'small': 14, 'normal': 16, 'large': 18, 'extra-large': 20 };

/** Changes the base size; every size in style.css given in "rem" follows it. */
function applyTextSize(name) {
  const size = TEXT_SIZES[name] ?? TEXT_SIZES.normal;
  document.documentElement.style.fontSize = `${size}px`;
}

/** Applies the saved settings, and wires the Settings dialog: a change applies at once and is saved. */
function setUpSettings() {
  const textSize = recall('text-size') ?? 'normal';
  applyTextSize(textSize);
  for (const radio of document.querySelectorAll('input[name="text-size"]')) {
    radio.checked = radio.value === textSize;
    radio.addEventListener('change', () => {
      applyTextSize(radio.value);
      remember('text-size', radio.value);
    });
  }
  $('settings-button').addEventListener('click', () => $('settings-dialog').showModal());
}

// ---------------------------------------------------------------- What the page is showing

const state = {
  tables: [],    // all tables, from GET /api/tables
  table: null,   // the chosen table
  query: '',     // the SELECT that filled the grid; it runs again after every change
  result: null,  // the last answer to that SELECT, to redraw the grid (e.g. after Cancel)
};

// ---------------------------------------------------------------- Sidebar

function renderTableList() {
  const list = $('table-list');
  list.replaceChildren();
  for (const table of state.tables) {
    const button = element('button', { type: 'button', className: table === state.table ? 'active' : '' },
      element('span', { textContent: table.name }),
      element('span', { className: 'count', textContent: `${table.columns.length} col.` }));
    button.addEventListener('click', () => chooseTable(table));
    list.append(element('li', {}, button));
  }
}

/** Reloads the list of tables (e.g. after a CREATE TABLE), keeping the chosen one. */
async function reloadTables() {
  state.tables = await fetchTables();
  state.table = state.tables.find((table) => table.name === state.table?.name) ?? null;
  renderTableList();
}

// ---------------------------------------------------------------- Choosing a table

function chooseTable(table) {
  state.table = table;
  remember('table', table.name);
  renderTableList();

  $('table-title').textContent = table.name;
  $('table-subtitle').textContent = table.readOnly
    ? 'The catalogue: it describes every table. Read-only here; edit its file by hand.'
    : `Columns: ${table.columns.map((column) => column.name).join(', ')}`;

  // Search: one choice per column, starting on the first column after the ID
  const select = $('search-column');
  select.replaceChildren(...table.columns.map((column) =>
    element('option', { value: column.name, textContent: column.name })));
  select.selectedIndex = table.autoId && table.columns.length > 1 ? 1 : 0;
  $('search-text').value = '';
  $('search-form').hidden = false;

  hideNewRow();
  loadRows(`SELECT * FROM ${table.name}`);
}

// ---------------------------------------------------------------- The rows

/** Runs a SELECT and draws its rows; `show` also displays the SQL above the grid. */
async function loadRows(sql, show = true) {
  state.query = sql;
  try {
    const [result] = await runSql(sql);
    if (show) showLastSql(sql, result);
    renderGrid(result);
  } catch (error) {
    toast(error.message, 'error');
  }
}

/** Shows the SQL of the last action and the server's answer. */
function showLastSql(sql, result) {
  $('last-sql').hidden = false;
  $('last-sql-text').textContent = sql;
  const badge = $('last-sql-result');
  badge.textContent = `${result.ok ? '✓' : '✗'} ${result.message}`;
  badge.className = `badge ${result.ok ? 'ok' : 'error'}`;
}

function renderGrid(result) {
  state.result = result;
  if (!result.ok || !result.columns) {
    $('grid').replaceChildren();
    $('grid-empty').hidden = true;
    return;
  }
  const count = fillTable($('grid'), result, true);
  $('grid-empty').hidden = count > 0;
}

/**
 * Fills a <table> with a result's columns and rows. With `withActions`, rows that have an ID
 * get Edit and Delete buttons (never for the read-only catalogue). Returns the number of rows.
 */
function fillTable(tableNode, result, withActions) {
  const canChange = withActions && result.idColumn !== null && state.table && !state.table.readOnly;
  const head = element('tr', {}, ...result.columns.map((column) => element('th', { textContent: column.name })));
  if (canChange) head.append(element('th', { className: 'row-actions' }));

  const body = element('tbody');
  for (const values of result.rows) {
    const row = element('tr');
    values.forEach((value, i) => row.append(element('td', { className: i === result.idColumn ? 'id' : '', textContent: value })));
    if (canChange) row.append(actionCell(result, values, row));
    body.append(row);
  }
  tableNode.replaceChildren(element('thead', {}, head), body);
  return result.rows.length;
}

function actionCell(result, values, row) {
  const editButton = element('button', { type: 'button', className: 'button small', textContent: 'Edit' });
  const deleteButton = element('button', { type: 'button', className: 'button small ghost link-danger', textContent: 'Delete' });
  editButton.addEventListener('click', () => startEdit(result, values, row));
  deleteButton.addEventListener('click', () => deleteRow(result, values));
  return element('td', { className: 'row-actions' }, element('div', { className: 'actions' }, editButton, deleteButton));
}

/** The condition that picks one row by its ID, e.g. "ID = 5". */
function whereId(result, values) {
  return `${result.columns[result.idColumn].name} = ${values[result.idColumn]}`;
}

// ---------------------------------------------------------------- Changing data

/** Sends an INSERT, UPDATE or DELETE, shows its answer, and reloads the rows if it worked. */
async function change(sql) {
  try {
    const [result] = await runSql(sql);
    showLastSql(sql, result);
    if (!result.ok) {
      toast(result.message, 'error');
      return false;
    }
    toast(result.message);
    await loadRows(state.query, false);
    return true;
  } catch (error) {
    toast(error.message, 'error');
    return false;
  }
}

/** Edit: the row's cells become input boxes. Enter or Save sends only the changed columns. */
function startEdit(result, values, row) {
  if ($('grid').querySelector('tr.editing')) return;  // one row at a time
  row.classList.add('editing');

  const inputs = [];
  result.columns.forEach((column, i) => {
    if (!column.editable) return;
    const input = element('input', { value: values[i], maxLength: column.width });
    input.dataset.column = column.name;
    input.dataset.original = values[i];
    input.addEventListener('keydown', (event) => {
      if (event.key === 'Enter') save();
      if (event.key === 'Escape') cancel();
    });
    row.cells[i].replaceChildren(input);
    inputs.push(input);
  });

  const saveButton = element('button', { type: 'button', className: 'button small primary', textContent: 'Save' });
  const cancelButton = element('button', { type: 'button', className: 'button small ghost', textContent: 'Cancel' });
  saveButton.addEventListener('click', save);
  cancelButton.addEventListener('click', cancel);
  row.cells[row.cells.length - 1].replaceChildren(element('div', { className: 'actions' }, saveButton, cancelButton));
  inputs[0]?.focus();

  function cancel() {
    renderGrid(state.result);
  }

  async function save() {
    const changes = inputs
      .filter((input) => input.value.trim() !== input.dataset.original)
      .map((input) => `${input.dataset.column} = ${quote(input.value.trim())}`);
    if (changes.length === 0) return cancel();
    await change(`UPDATE ${state.table.name} SET ${changes.join(', ')} WHERE ${whereId(result, values)}`);
  }
}

/** Delete: asks first, in a dialog. */
async function deleteRow(result, values) {
  const summary = values.filter((value, i) => i !== result.idColumn && value !== '').slice(0, 3).join(' · ');
  $('confirm-text').textContent = `Row ${values[result.idColumn]}: ${summary}`;
  const dialog = $('confirm-dialog');
  dialog.returnValue = '';
  dialog.showModal();
  await new Promise((resolve) => dialog.addEventListener('close', resolve, { once: true }));
  if (dialog.returnValue === 'ok') {
    await change(`DELETE FROM ${state.table.name} WHERE ${whereId(result, values)}`);
  }
}

/** New row: one input per column (the ID is left out when it is assigned automatically). */
function showNewRow() {
  const table = state.table;
  const columns = table.columns.filter((column, i) => !(table.autoId && i === 0));
  $('new-row-fields').replaceChildren(...columns.map((column) =>
    element('label', { className: 'field' },
      element('span', { textContent: column.name }),
      element('input', { name: column.name, maxLength: column.width }))));
  $('new-row-hint').textContent = table.autoId ? 'The ID is assigned automatically.' : '';
  $('new-row-form').hidden = false;
  $('new-row-button').hidden = true;
  $('new-row-fields').querySelector('input')?.focus();
}

function hideNewRow() {
  $('new-row-form').hidden = true;
  $('new-row-button').hidden = !state.table || state.table.readOnly;
}

async function addRow(event) {
  event.preventDefault();
  const values = [...$('new-row-fields').querySelectorAll('input')].map((input) => quote(input.value.trim()));
  if (await change(`INSERT INTO ${state.table.name} VALUES (${values.join(', ')})`)) {
    hideNewRow();
    // Clear any search, so that the new row (always the last one) is visible
    $('search-text').value = '';
    await loadRows(`SELECT * FROM ${state.table.name}`, false);
    $('grid').querySelector('tbody tr:last-child')?.scrollIntoView({ block: 'nearest' });
  }
}

// ---------------------------------------------------------------- Search

function search(event) {
  event.preventDefault();
  const table = state.table.name;
  const column = $('search-column').value;
  const text = $('search-text').value.trim();
  loadRows(text === '' ? `SELECT * FROM ${table}` : `SELECT * FROM ${table} WHERE ${column} LIKE ${quote('%' + text + '%')}`);
}

function showAll() {
  $('search-text').value = '';
  loadRows(`SELECT * FROM ${state.table.name}`);
}

// ---------------------------------------------------------------- New table

/** Width of the ID column that "Number the rows automatically" adds, as in the sample tables. */
const ID_WIDTH = 10;

/** Opens the New table dialog with an empty name and one empty column. */
function showNewTable() {
  $('new-table-name').value = '';
  $('new-table-id').checked = true;
  $('new-table-columns').replaceChildren();
  addColumnRow();
  updateNewTable();
  $('new-table-dialog').showModal();
  $('new-table-name').focus();
}

/** Adds one line to the column list: name, type, width, and a button that removes the line. */
function addColumnRow() {
  const name = element('input', { required: true, maxLength: 20, pattern: '[A-Za-z][A-Za-z0-9_\\-]*',
    title: 'Letters, digits, _ and -, starting with a letter', placeholder: 'e.g. MODEL', autocomplete: 'off' });
  const type = element('select', {},
    element('option', { value: 'CHAR', textContent: 'Text' }),
    element('option', { value: 'NUMBER', textContent: 'Number' }));
  const width = element('input', { type: 'number', required: true, min: 1, max: 999, value: 20 });
  const remove = element('button', { type: 'button', className: 'button small ghost link-danger', textContent: '✕', title: 'Remove this column' });
  name.setAttribute('aria-label', 'Column name');
  type.setAttribute('aria-label', 'Type');
  width.setAttribute('aria-label', 'Width in characters');
  remove.addEventListener('click', () => {
    row.remove();
    updateNewTable();
  });
  const row = element('div', { className: 'column-row' }, name, type, width, remove);
  $('new-table-columns').append(row);
  updateNewTable();
  return name;
}

/** The CREATE TABLE for what the form holds now, and the row length it gives. */
function newTableSql() {
  const columns = [];
  let rowLength = 0;
  if ($('new-table-id').checked) {
    columns.push(`ID NUMBER(${ID_WIDTH})`);
    rowLength += ID_WIDTH;
  }
  for (const row of $('new-table-columns').children) {
    const [name, type, width] = row.querySelectorAll('input, select');
    columns.push(`${name.value.trim().toUpperCase() || '?'} ${type.value}(${width.value || '?'})`);
    rowLength += Number(width.value) || 0;
  }
  const table = $('new-table-name').value.trim().toUpperCase() || '?';
  return { sql: `CREATE TABLE ${table} (${columns.join(', ')})`, rowLength };
}

/** Shows the SQL while you type; the last column can't be removed. */
function updateNewTable() {
  const { sql, rowLength } = newTableSql();
  $('new-table-sql').textContent = sql;
  $('new-table-length').textContent = `row length ${rowLength} characters`;
  $('new-table-error').hidden = true;
  const rows = $('new-table-columns').children;
  for (const row of rows) row.querySelector('button').disabled = rows.length === 1;
}

/** Sends the CREATE TABLE; on success closes the dialog and opens the new, empty table. */
async function createTable(event) {
  event.preventDefault();
  const { sql } = newTableSql();
  try {
    const [result] = await runSql(sql);
    if (!result.ok) {
      $('new-table-error').textContent = result.message;
      $('new-table-error').hidden = false;
      return;
    }
    $('new-table-dialog').close();
    toast(result.message);
    await reloadTables();
    const table = state.tables.find((t) => t.name === result.table);
    if (table) chooseTable(table);
  } catch (error) {
    toast(error.message, 'error');
  }
}

// ---------------------------------------------------------------- SQL console

function resultCard(result) {
  const card = element('div', { className: 'result' },
    element('div', { className: 'result-head' },
      element('code', { textContent: result.sql }),
      element('span', { className: `badge ${result.ok ? 'ok' : 'error'}`, textContent: `${result.ok ? '✓' : '✗'} ${result.message}` })));
  if (result.columns) {
    const table = element('table', { className: 'grid' });
    fillTable(table, result, false);
    card.append(element('div', { className: 'grid-wrap' }, table));
  }
  return card;
}

async function runConsole(event) {
  event.preventDefault();
  const sql = $('console-input').value;
  if (sql.trim() === '') return;
  try {
    const results = await runSql(sql);
    const cards = results.length > 0
      ? results.map(resultCard)
      : [element('p', { className: 'muted', textContent: 'Nothing to show: only comments or COLUMN settings.' })];
    $('console-results').prepend(...cards);  // newest first
    // A new table appears in the list; if a statement changed the table on screen, redraw it
    if (results.some((result) => result.ok && /^create\b/i.test(result.sql))) {
      await reloadTables();
    }
    if (state.table && results.some((result) => result.ok && !result.columns && result.table === state.table.name)) {
      await loadRows(state.query, false);
    }
  } catch (error) {
    toast(error.message, 'error');
  }
}

// ---------------------------------------------------------------- Start

async function start() {
  setUpSettings();
  $('new-row-button').addEventListener('click', showNewRow);
  $('new-row-cancel').addEventListener('click', hideNewRow);
  $('new-row-form').addEventListener('submit', addRow);
  $('new-table-button').addEventListener('click', showNewTable);
  $('new-table-add-column').addEventListener('click', () => addColumnRow().focus());
  $('new-table-cancel').addEventListener('click', () => $('new-table-dialog').close());
  $('new-table-form').addEventListener('input', updateNewTable);
  $('new-table-form').addEventListener('submit', createTable);
  $('search-form').addEventListener('submit', search);
  $('search-clear').addEventListener('click', showAll);
  $('console-form').addEventListener('submit', runConsole);
  $('console-clear').addEventListener('click', () => $('console-results').replaceChildren());
  $('console-input').addEventListener('keydown', (event) => {
    if (event.key === 'Enter' && (event.ctrlKey || event.metaKey)) $('console-form').requestSubmit();
  });

  try {
    state.tables = await fetchTables();
  } catch (error) {
    toast(error.message, 'error');
    return;
  }
  const saved = recall('table');
  const first = state.tables.find((table) => table.name === saved)
             ?? state.tables.find((table) => !table.readOnly)
             ?? state.tables[0];
  renderTableList();
  if (first) chooseTable(first);
}

start();
