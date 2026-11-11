// comctl_treeview.cpp - the tree view: a hierarchy of items that open
// and close, with lines and buttons, selection, keyboard and mouse
// handling, label editing, sorting and callbacks.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "comctl.h"

#include <string.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

using namespace winport;

const int kAutoChildren = -2;  // cChildren never set: the real children decide

// HTREEITEM points at one of these.
struct _TREEITEM : HTREEITEM__ {
  _TREEITEM *parent = NULL, *first = NULL, *last = NULL, *next = NULL, *prev = NULL;
  std::wstring text;
  bool callback = false;
  int image = 0, selImage = 0;
  int children = kAutoChildren;
  LPARAM param = 0;
  UINT state = 0;
  int integral = 1;
};

namespace {

typedef _TREEITEM Node;

const UINT_PTR kEditTimer = 1;
const UINT_PTR kEditSubclass = 0x5456;

struct Tree {
  HWND hwnd = NULL, edit = NULL, tooltips = NULL;
  Node root;  // the parent of the top-level items
  std::unordered_set<HTREEITEM> all;
  Node *caret = NULL, *drop = NULL, *editItem = NULL, *insertMark = NULL;
  bool insertAfter = true;
  HIMAGELIST normal = NULL, state = NULL;
  int indent = 19, itemHeight = 0;  // 0 for the default
  COLORREF bk = CLR_DEFAULT, text = CLR_DEFAULT, line = CLR_DEFAULT, markColor = CLR_DEFAULT;
  HFONT font = NULL, bold = NULL;
  bool unicodeNotify = true;
  std::vector<Node*> rows;  // the items that show, in order
  bool rowsDirty = true;
  int top = 0;      // the first row shown
  int scrollX = 0;  // in pixels
  // The mouse.
  Node* pressItem = NULL;
  POINT pressPt = {0, 0};
  bool pressRight = false, tracking = false, pressWasCaret = false, pressOnLabel = false;
  Node* pendingEdit = NULL;
  // Typing to find an item.
  std::wstring search;
  DWORD searchTime = 0;
  bool ending = false;
  UINT scrollTime = 300;
};

Tree* StateOf(HWND h) { return (Tree*)GetWindowLongPtrW(h, 0); }
DWORD StyleOf(HWND h) { return (DWORD)GetWindowLongW(h, GWL_STYLE); }
bool HasStyle(Tree* t, DWORD s) { return (StyleOf(t->hwnd) & s) != 0; }
HFONT FontOf(Tree* t) { return t->font ? t->font : ControlFont(); }
bool HasFocus(Tree* t) { return GetFocus() == t->hwnd; }
COLORREF BkColor(Tree* t) { return t->bk == CLR_DEFAULT ? GetSysColor(COLOR_WINDOW) : t->bk; }
COLORREF TextColor(Tree* t) { return t->text == CLR_DEFAULT ? GetSysColor(COLOR_WINDOWTEXT) : t->text; }
COLORREF LineColor(Tree* t) { return t->line == CLR_DEFAULT ? GetSysColor(COLOR_GRAYTEXT) : t->line; }

Node* Valid(Tree* t, HTREEITEM h) { return h && t->all.count(h) ? static_cast<Node*>(h) : NULL; }

void FillSolid(HDC dc, const RECT& r, COLORREF c) {
  HBRUSH b = CreateSolidBrush(c);
  FillRect(dc, &r, b);
  DeleteObject(b);
}

HFONT BoldFont(Tree* t) {
  if (!t->bold) {
    LOGFONTW lf;
    GetObjectW(FontOf(t), sizeof lf, &lf);
    lf.lfWeight = FW_BOLD;
    t->bold = CreateFontIndirectW(&lf);
  }
  return t->bold;
}

int Depth(Node* n) {
  int d = 0;
  for (Node* p = n->parent; p && p->parent; p = p->parent) d++;
  return d;
}

bool IsAncestor(Node* a, Node* n) {
  for (Node* p = n ? n->parent : NULL; p; p = p->parent)
    if (p == a) return true;
  return false;
}

// ---------------------------------------------------------------------
// Notifications

LRESULT Notify(Tree* t, UINT code, NMHDR* nm = NULL) { return SendNotify(t->hwnd, NULL, code, nm); }
UINT Code(Tree* t, UINT w, UINT a) { return t->unicodeNotify ? w : a; }

void Describe(Node* n, TVITEMW* it) {
  if (!n) return;
  it->mask = TVIF_HANDLE | TVIF_STATE | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
  it->hItem = n;
  it->state = n->state;
  it->stateMask = 0xFFFF;
  it->lParam = n->param;
  it->iImage = n->image;
  it->iSelectedImage = n->selImage;
}

LRESULT NotifyTree(Tree* t, UINT codeW, UINT codeA, UINT action, Node* oldItem, Node* newItem,
                   POINT pt = POINT{0, 0}) {
  NMTREEVIEWW nm = {};
  nm.action = action;
  Describe(oldItem, &nm.itemOld);
  Describe(newItem, &nm.itemNew);
  nm.ptDrag = pt;
  return Notify(t, Code(t, codeW, codeA), &nm.hdr);
}

struct Shown {
  std::wstring text;
  int image = 0, selImage = 0, children = 0;
};

// What an item shows, asking the parent for what it keeps itself.
Shown Resolve(Tree* t, Node* n, UINT want) {
  Shown s;
  s.text = n->text;
  s.image = n->image;
  s.selImage = n->selImage;
  s.children = n->children == kAutoChildren ? (n->first ? 1 : 0) : n->children;
  UINT ask = 0;
  if ((want & TVIF_TEXT) && n->callback) ask |= TVIF_TEXT;
  if ((want & TVIF_IMAGE) && n->image == I_IMAGECALLBACK) ask |= TVIF_IMAGE;
  if ((want & TVIF_SELECTEDIMAGE) && n->selImage == I_IMAGECALLBACK) ask |= TVIF_SELECTEDIMAGE;
  if ((want & TVIF_CHILDREN) && n->children == I_CHILDRENCALLBACK) ask |= TVIF_CHILDREN;
  if (!ask) return s;
  bool u = t->unicodeNotify;
  NMTVDISPINFOW di = {};
  wchar_t wbuf[1024];
  char abuf[1024];
  di.item.mask = ask;
  di.item.hItem = n;
  di.item.state = n->state;
  di.item.lParam = n->param;
  if (ask & TVIF_TEXT) {
    wbuf[0] = 0;
    abuf[0] = 0;
    di.item.pszText = u ? wbuf : (LPWSTR)abuf;
    di.item.cchTextMax = 1024;
  }
  Notify(t, Code(t, TVN_GETDISPINFOW, TVN_GETDISPINFOA), &di.hdr);
  if (!t->all.count(n)) return s;
  if (ask & TVIF_TEXT) s.text = TextIn(di.item.pszText, u);
  if (ask & TVIF_IMAGE) s.image = di.item.iImage;
  if (ask & TVIF_SELECTEDIMAGE) s.selImage = di.item.iSelectedImage;
  if (ask & TVIF_CHILDREN) s.children = di.item.cChildren;
  if (di.item.mask & TVIF_DI_SETITEM) {
    if (ask & TVIF_TEXT) {
      n->text = s.text;
      n->callback = false;
    }
    if (ask & TVIF_IMAGE) n->image = s.image;
    if (ask & TVIF_SELECTEDIMAGE) n->selImage = s.selImage;
    if (ask & TVIF_CHILDREN) n->children = s.children;
  }
  return s;
}

std::wstring TextOf(Tree* t, Node* n) { return Resolve(t, n, TVIF_TEXT).text; }
bool HasChildren(Tree* t, Node* n) { return Resolve(t, n, TVIF_CHILDREN).children != 0; }

// ---------------------------------------------------------------------
// Layout

int FontHeight(Tree* t) {
  HDC dc = GetDC(t->hwnd);
  HGDIOBJ old = SelectObject(dc, FontOf(t));
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  ReleaseDC(t->hwnd, dc);
  return tm.tmHeight;
}

int TextWidth(Tree* t, const std::wstring& s, bool bold) {
  HDC dc = GetDC(t->hwnd);
  HGDIOBJ old = SelectObject(dc, bold ? BoldFont(t) : FontOf(t));
  SIZE sz = {0, 0};
  GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
  SelectObject(dc, old);
  ReleaseDC(t->hwnd, dc);
  return sz.cx;
}

SIZE ImageSize(HIMAGELIST il) {
  int cx = 0, cy = 0;
  if (il) ImageList_GetIconSize(il, &cx, &cy);
  return SIZE{cx, cy};
}

SIZE StateSize(Tree* t) {
  if (t->state) return ImageSize(t->state);
  if (HasStyle(t, TVS_CHECKBOXES)) return SIZE{16, 16};
  return SIZE{0, 0};
}

int ItemHeight(Tree* t) {
  if (t->itemHeight > 0) return t->itemHeight;
  int h = std::max(FontHeight(t) + 3, (int)std::max(ImageSize(t->normal).cy, StateSize(t).cy) + 1);
  if (!HasStyle(t, TVS_NONEVENHEIGHT) && (h & 1)) h++;
  return h;
}

int Indent(Tree* t) { return std::max(t->indent, (int)ImageSize(t->normal).cx + 3); }

void Flatten(Tree* t, Node* parent) {
  for (Node* n = parent->first; n; n = n->next) {
    t->rows.push_back(n);
    if ((n->state & TVIS_EXPANDED) && n->first) Flatten(t, n);
  }
}

std::vector<Node*>& Rows(Tree* t) {
  if (t->rowsDirty) {
    t->rows.clear();
    Flatten(t, &t->root);
    t->rowsDirty = false;
  }
  return t->rows;
}

int RowOf(Tree* t, Node* n) {
  std::vector<Node*>& rows = Rows(t);
  auto it = std::find(rows.begin(), rows.end(), n);
  return it == rows.end() ? -1 : (int)(it - rows.begin());
}

int VisibleCount(Tree* t) {
  RECT c;
  GetClientRect(t->hwnd, &c);
  return std::max(0, (int)(c.bottom / ItemHeight(t)));
}

struct Rects {
  RECT line, button, state, icon, label;
  bool hasButton;
};

Rects ItemRects(Tree* t, Node* n, int row) {
  Rects r;
  int h = ItemHeight(t), ind = Indent(t);
  int y = (row - t->top) * h;
  RECT c;
  GetClientRect(t->hwnd, &c);
  r.line = RECT{0, y, c.right, y + h};
  int level = Depth(n) + (HasStyle(t, TVS_LINESATROOT) ? 1 : 0);
  int x = level * ind - t->scrollX;
  r.button = RECT{x - ind, y, x, y + h};
  r.hasButton = HasStyle(t, TVS_HASBUTTONS) && level > 0 && HasChildren(t, n);
  SIZE st = StateSize(t), im = ImageSize(t->normal);
  r.state = RECT{x, y, x + st.cx, y + h};
  r.icon = RECT{r.state.right, y, r.state.right + im.cx + (im.cx ? 3 : 0), y + h};
  int w = TextWidth(t, TextOf(t, n), (n->state & TVIS_BOLD) != 0) + 4;
  r.label = RECT{r.icon.right, y, r.icon.right + w, y + h};
  return r;
}

int ContentWidth(Tree* t) {
  int w = 0;
  std::vector<Node*>& rows = Rows(t);
  int saved = t->scrollX;
  t->scrollX = 0;
  for (int i = 0, n = std::min((int)rows.size(), 5000); i < n; i++)
    w = std::max(w, (int)ItemRects(t, rows[i], i).label.right);
  t->scrollX = saved;
  return w;
}

void UpdateScroll(Tree* t) {
  RECT c;
  GetClientRect(t->hwnd, &c);
  int rows = (int)Rows(t).size(), per = VisibleCount(t);
  t->top = std::max(0, std::min(t->top, rows - std::max(per, 1)));
  if (HasStyle(t, TVS_NOSCROLL)) {
    ShowScrollBar(t->hwnd, SB_BOTH, FALSE);
    return;
  }
  SCROLLINFO v = {sizeof v, SIF_RANGE | SIF_PAGE | SIF_POS};
  v.nMax = rows - 1;
  v.nPage = std::max(per, 1);
  v.nPos = t->top;
  SetScrollInfo(t->hwnd, SB_VERT, &v, TRUE);
  GetClientRect(t->hwnd, &c);
  int w = ContentWidth(t);
  t->scrollX = std::max(0, std::min(t->scrollX, w - (int)c.right));
  SCROLLINFO hz = {sizeof hz, SIF_RANGE | SIF_PAGE | SIF_POS};
  hz.nMax = w - 1;
  hz.nPage = c.right;
  hz.nPos = t->scrollX;
  SetScrollInfo(t->hwnd, SB_HORZ, &hz, TRUE);
}

void Changed(Tree* t) {
  t->rowsDirty = true;
  UpdateScroll(t);
  InvalidateRect(t->hwnd, NULL, TRUE);
}

void InvalidateItem(Tree* t, Node* n) {
  int row = RowOf(t, n);
  if (row < 0) return;
  Rects r = ItemRects(t, n, row);
  InvalidateRect(t->hwnd, &r.line, TRUE);
}

void ScrollToRow(Tree* t, int top) {
  t->top = top;
  UpdateScroll(t);
  InvalidateRect(t->hwnd, NULL, TRUE);
}

// ---------------------------------------------------------------------
// Opening and closing

bool Select(Tree* t, Node* n, UINT action);
void EndEdit(Tree* t, bool save);

bool Expand(Tree* t, Node* n, UINT code, bool byUser) {
  if (!n || n == &t->root) return false;
  UINT op = code & 3;
  bool open = (n->state & TVIS_EXPANDED) != 0;
  if (op == TVE_TOGGLE) op = open ? TVE_COLLAPSE : TVE_EXPAND;
  if (op == TVE_EXPAND && open) return false;
  if (op == TVE_COLLAPSE && !open && !(code & TVE_COLLAPSERESET)) return false;
  if (op == TVE_EXPAND && !HasChildren(t, n)) return false;
  // TVM_EXPAND tells the parent only the first time an item opens.
  bool tell = byUser || !(n->state & TVIS_EXPANDEDONCE) || op == TVE_COLLAPSE;
  if (!byUser && op == TVE_COLLAPSE && !(n->state & TVIS_EXPANDEDONCE)) tell = false;
  if (tell && NotifyTree(t, TVN_ITEMEXPANDINGW, TVN_ITEMEXPANDINGA, op, NULL, n)) return false;
  if (!t->all.count(n)) return false;
  if (op == TVE_EXPAND) {
    n->state |= TVIS_EXPANDED | TVIS_EXPANDEDONCE;
  } else {
    n->state &= ~TVIS_EXPANDED;
    if (t->caret && IsAncestor(n, t->caret)) Select(t, n, TVC_UNKNOWN);
    if (code & TVE_COLLAPSERESET) {
      n->state &= ~TVIS_EXPANDEDONCE;
      while (n->first) SendMessageW(t->hwnd, TVM_DELETEITEM, 0, (LPARAM)n->first);
    }
  }
  Changed(t);
  if (tell) NotifyTree(t, TVN_ITEMEXPANDEDW, TVN_ITEMEXPANDEDA, op, NULL, n);
  return true;
}

void ExpandAll(Tree* t, Node* n) {
  Expand(t, n, TVE_EXPAND, true);
  for (Node* c = n->first; c; c = c->next) ExpandAll(t, c);
}

bool EnsureVisible(Tree* t, Node* n) {
  if (!n) return false;
  for (Node* p = n->parent; p && p != &t->root; p = p->parent)
    if (!(p->state & TVIS_EXPANDED)) Expand(t, p, TVE_EXPAND, true);
  int row = RowOf(t, n), per = std::max(1, VisibleCount(t));
  if (row < 0) return false;
  if (row < t->top)
    ScrollToRow(t, row);
  else if (row >= t->top + per)
    ScrollToRow(t, row - per + 1);
  return true;
}

// ---------------------------------------------------------------------
// Selection

bool Select(Tree* t, Node* n, UINT action) {
  if (n == t->caret) {
    if (n) EnsureVisible(t, n);
    return true;
  }
  Node* old = t->caret;
  if (NotifyTree(t, TVN_SELCHANGINGW, TVN_SELCHANGINGA, action, old, n)) return false;
  if (n && !t->all.count(n)) return false;
  if (old && t->all.count(old)) {
    old->state &= ~TVIS_SELECTED;
    InvalidateItem(t, old);
  }
  t->caret = n;
  if (n) {
    n->state |= TVIS_SELECTED;
    if (HasStyle(t, TVS_SINGLEEXPAND) && action != TVC_UNKNOWN) {
      if (!NotifyTree(t, TVN_SINGLEEXPAND, TVN_SINGLEEXPAND, TVE_EXPAND, old, n)) {
        if (old && t->all.count(old) && !IsAncestor(old, n)) Expand(t, old, TVE_COLLAPSE, true);
        Expand(t, n, TVE_EXPAND, true);
      }
    }
    EnsureVisible(t, n);
    InvalidateItem(t, n);
  }
  NotifyTree(t, TVN_SELCHANGEDW, TVN_SELCHANGEDA, action, old && t->all.count(old) ? old : NULL, n);
  return true;
}

void SetDrop(Tree* t, Node* n) {
  if (t->drop && t->all.count(t->drop)) {
    t->drop->state &= ~TVIS_DROPHILITED;
    InvalidateItem(t, t->drop);
  }
  t->drop = n;
  if (n) {
    n->state |= TVIS_DROPHILITED;
    InvalidateItem(t, n);
  }
}

bool IsChecked(Node* n) { return ((n->state & TVIS_STATEIMAGEMASK) >> 12) == 2; }

void ToggleCheck(Tree* t, Node* n) {
  n->state = (n->state & ~TVIS_STATEIMAGEMASK) | INDEXTOSTATEIMAGEMASK(IsChecked(n) ? 1 : 2);
  InvalidateItem(t, n);
}

// ---------------------------------------------------------------------
// Items

void Link(Node* parent, Node* n, Node* after) {
  n->parent = parent;
  if (!after) {  // first
    n->next = parent->first;
    n->prev = NULL;
    if (parent->first) parent->first->prev = n;
    parent->first = n;
    if (!parent->last) parent->last = n;
    return;
  }
  n->prev = after;
  n->next = after->next;
  if (after->next)
    after->next->prev = n;
  else
    parent->last = n;
  after->next = n;
}

void Unlink(Node* n) {
  Node* p = n->parent;
  if (n->prev)
    n->prev->next = n->next;
  else
    p->first = n->next;
  if (n->next)
    n->next->prev = n->prev;
  else
    p->last = n->prev;
  n->next = n->prev = NULL;
}

Node* InsertItem(Tree* t, const TVINSERTSTRUCTW* in, bool u) {
  if (!in) return NULL;
  Node* parent = (in->hParent == TVI_ROOT || !in->hParent) ? &t->root : Valid(t, in->hParent);
  if (!parent) return NULL;
  const TVITEMEXW& it = in->itemex;
  Node* n = new Node;
  if (it.mask & TVIF_TEXT) {
    n->callback = IsCallback(it.pszText, u);
    n->text = TextIn(it.pszText, u);
  }
  if (it.mask & TVIF_IMAGE) n->image = it.iImage;
  if (it.mask & TVIF_SELECTEDIMAGE) n->selImage = it.iSelectedImage;
  if (it.mask & TVIF_CHILDREN) n->children = it.cChildren;
  if (it.mask & TVIF_PARAM) n->param = it.lParam;
  if (it.mask & TVIF_STATE) n->state = it.state & it.stateMask & ~(TVIS_SELECTED | TVIS_DROPHILITED);
  if (it.mask & TVIF_INTEGRAL) n->integral = std::max(1, it.iIntegral);
  if (HasStyle(t, TVS_CHECKBOXES) && !(n->state & TVIS_STATEIMAGEMASK)) n->state |= INDEXTOSTATEIMAGEMASK(1);
  Node* after;
  HTREEITEM where = in->hInsertAfter;
  if (where == TVI_FIRST) {
    after = NULL;
  } else if (where == TVI_SORT) {
    after = NULL;
    for (Node* c = parent->first; c; c = c->next) {
      if (!n->callback && !c->callback && _wcsicmp(n->text.c_str(), c->text.c_str()) < 0) break;
      after = c;
    }
  } else if (where == TVI_LAST || !where) {
    after = parent->last;
  } else {
    after = Valid(t, where);
    if (!after || after->parent != parent) after = parent->last;
  }
  Link(parent, n, after);
  t->all.insert(n);
  Changed(t);
  return n;
}

void DeleteTree(Tree* t, Node* n) {
  while (n->first) DeleteTree(t, n->first);
  if (n == &t->root) return;
  NotifyTree(t, TVN_DELETEITEMW, TVN_DELETEITEMA, 0, n, NULL);
  if (n == t->editItem) EndEdit(t, false);
  if (n == t->drop) t->drop = NULL;
  if (n == t->insertMark) t->insertMark = NULL;
  if (n == t->pressItem) t->pressItem = NULL, t->tracking = false;
  if (n == t->pendingEdit) t->pendingEdit = NULL;
  if (n == t->caret) t->caret = NULL;
  Unlink(n);
  t->all.erase(n);
  delete n;
}

bool DeleteItem(Tree* t, HTREEITEM h) {
  if (h == TVI_ROOT || !h) {
    Node* old = t->caret;
    DeleteTree(t, &t->root);
    t->top = 0;
    t->scrollX = 0;
    Changed(t);
    if (old) NotifyTree(t, TVN_SELCHANGEDW, TVN_SELCHANGEDA, TVC_UNKNOWN, NULL, NULL);
    return true;
  }
  Node* n = Valid(t, h);
  if (!n) return false;
  // The selection moves off a deleted branch to a neighbor.
  bool hadCaret = t->caret && (t->caret == n || IsAncestor(n, t->caret));
  Node* after = n->next ? n->next : n->prev ? n->prev : (n->parent != &t->root ? n->parent : NULL);
  if (hadCaret) {
    t->caret->state &= ~TVIS_SELECTED;
    t->caret = NULL;
  }
  Node* parent = n->parent;
  DeleteTree(t, n);
  if (parent != &t->root && !parent->first && parent->children == kAutoChildren) parent->state &= ~TVIS_EXPANDED;
  Changed(t);
  if (hadCaret) {
    if (after && t->all.count(after))
      Select(t, after, TVC_UNKNOWN);
    else
      NotifyTree(t, TVN_SELCHANGEDW, TVN_SELCHANGEDA, TVC_UNKNOWN, NULL, NULL);
  }
  return true;
}

bool SetItem(Tree* t, const TVITEMEXW* in, bool u) {
  if (!in) return false;
  Node* n = Valid(t, in->hItem);
  if (!n) return false;
  if (in->mask & TVIF_TEXT) {
    n->callback = IsCallback(in->pszText, u);
    n->text = TextIn(in->pszText, u);
  }
  if (in->mask & TVIF_IMAGE) n->image = in->iImage;
  if (in->mask & TVIF_SELECTEDIMAGE) n->selImage = in->iSelectedImage;
  if (in->mask & TVIF_CHILDREN) n->children = in->cChildren;
  if (in->mask & TVIF_PARAM) n->param = in->lParam;
  if (in->mask & TVIF_INTEGRAL) n->integral = std::max(1, in->iIntegral);
  if (in->mask & TVIF_STATE) {
    UINT mask = in->stateMask;
    if (mask & TVIS_EXPANDED) {
      bool want = (in->state & TVIS_EXPANDED) != 0;
      if (want != ((n->state & TVIS_EXPANDED) != 0)) t->rowsDirty = true;
    }
    if (mask & TVIS_BOLD) t->rowsDirty = true;
    n->state = (n->state & ~mask) | (in->state & mask);
    if ((mask & TVIS_DROPHILITED) && (in->state & TVIS_DROPHILITED)) t->drop = n;
  }
  Changed(t);
  return true;
}

bool GetItem(Tree* t, TVITEMEXW* out, bool u) {
  if (!out) return false;
  Node* n = Valid(t, out->hItem);
  if (!n) return false;
  Shown s = Resolve(t, n, out->mask);
  if (out->mask & TVIF_TEXT) CopyOut(s.text, out->pszText, out->cchTextMax, u);
  if (out->mask & TVIF_IMAGE) out->iImage = s.image;
  if (out->mask & TVIF_SELECTEDIMAGE) out->iSelectedImage = s.selImage;
  if (out->mask & TVIF_CHILDREN) out->cChildren = s.children;
  if (out->mask & TVIF_PARAM) out->lParam = n->param;
  if (out->mask & TVIF_STATE) out->state = n->state & out->stateMask;
  if (out->mask & TVIF_INTEGRAL) out->iIntegral = n->integral;
  return true;
}

Node* NextVisible(Tree* t, Node* n) {
  int r = RowOf(t, n);
  if (r < 0 || r + 1 >= (int)Rows(t).size()) return NULL;
  return Rows(t)[r + 1];
}

Node* PrevVisible(Tree* t, Node* n) {
  int r = RowOf(t, n);
  if (r <= 0) return NULL;
  return Rows(t)[r - 1];
}

Node* NextItem(Tree* t, HTREEITEM h, UINT code) {
  Node* n = Valid(t, h);
  switch (code) {
    case TVGN_ROOT: return t->root.first;
    case TVGN_CARET: return t->caret;
    case TVGN_DROPHILITE: return t->drop;
    case TVGN_FIRSTVISIBLE: return t->top < (int)Rows(t).size() ? Rows(t)[t->top] : NULL;
    case TVGN_LASTVISIBLE: return Rows(t).empty() ? NULL : Rows(t).back();
    case TVGN_CHILD:
      if (!h || h == TVI_ROOT) return t->root.first;
      return n ? n->first : NULL;
  }
  if (!n) return NULL;
  switch (code) {
    case TVGN_NEXT: return n->next;
    case TVGN_PREVIOUS: return n->prev;
    case TVGN_PARENT: return n->parent == &t->root ? NULL : n->parent;
    case TVGN_NEXTVISIBLE: return NextVisible(t, n);
    case TVGN_PREVIOUSVISIBLE: return PrevVisible(t, n);
  }
  return NULL;
}

template <class Less>
void SortChildren(Tree* t, Node* parent, bool recurse, Less less) {
  std::vector<Node*> kids;
  for (Node* c = parent->first; c; c = c->next) kids.push_back(c);
  std::stable_sort(kids.begin(), kids.end(), less);
  parent->first = parent->last = NULL;
  Node* prev = NULL;
  for (Node* c : kids) {
    c->prev = prev;
    c->next = NULL;
    if (prev)
      prev->next = c;
    else
      parent->first = c;
    prev = c;
  }
  parent->last = prev;
  if (recurse)
    for (Node* c : kids) SortChildren(t, c, true, less);
}

Node* ParentFor(Tree* t, HTREEITEM h) { return (!h || h == TVI_ROOT) ? &t->root : Valid(t, h); }

// ---------------------------------------------------------------------
// Hit testing

Node* HitTest(Tree* t, POINT pt, UINT* flags) {
  RECT c;
  GetClientRect(t->hwnd, &c);
  *flags = 0;
  if (pt.x < 0) *flags |= TVHT_TOLEFT;
  if (pt.x >= c.right) *flags |= TVHT_TORIGHT;
  if (pt.y < 0) *flags |= TVHT_ABOVE;
  if (pt.y >= c.bottom) *flags |= TVHT_BELOW;
  if (*flags) return NULL;
  int row = t->top + pt.y / ItemHeight(t);
  std::vector<Node*>& rows = Rows(t);
  if (row < 0 || row >= (int)rows.size()) {
    *flags = TVHT_NOWHERE;
    return NULL;
  }
  Node* n = rows[row];
  Rects r = ItemRects(t, n, row);
  if (r.hasButton && PtInRect(&r.button, pt))
    *flags = TVHT_ONITEMBUTTON;
  else if (pt.x < r.state.left)
    *flags = TVHT_ONITEMINDENT;
  else if (PtInRect(&r.state, pt))
    *flags = TVHT_ONITEMSTATEICON;
  else if (PtInRect(&r.icon, pt))
    *flags = TVHT_ONITEMICON;
  else if (PtInRect(&r.label, pt))
    *flags = TVHT_ONITEMLABEL;
  else
    *flags = TVHT_ONITEMRIGHT;
  return n;
}

// ---------------------------------------------------------------------
// Label editing

LRESULT CALLBACK EditSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
  Tree* t = (Tree*)data;
  switch (msg) {
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_HASSETSEL | DLGC_WANTCHARS;
    case WM_KEYDOWN:
      if (wp == VK_RETURN || wp == VK_ESCAPE) {
        EndEdit(t, wp == VK_RETURN);
        return 0;
      }
      break;
    case WM_CHAR:
      if (wp == '\r' || wp == 0x1B) return 0;
      break;
    case WM_KILLFOCUS:
      DefSubclassProc(h, msg, wp, lp);
      if (t->edit == h) EndEdit(t, true);
      return 0;
  }
  return DefSubclassProc(h, msg, wp, lp);
}

HWND BeginEdit(Tree* t, Node* n) {
  if (t->edit) EndEdit(t, true);
  if (!n) return NULL;
  EnsureVisible(t, n);
  int row = RowOf(t, n);
  if (row < 0) return NULL;
  Rects r = ItemRects(t, n, row);
  std::wstring text = TextOf(t, n);
  RECT c;
  GetClientRect(t->hwnd, &c);
  int w = std::max((int)(r.label.right - r.label.left) + 16, 60);
  w = std::min(w, std::max(30, (int)(c.right - r.label.left)));
  HWND e = CreateWindowExW(0, L"Edit", text.c_str(), WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, r.label.left - 1,
                           r.label.top, w, r.label.bottom - r.label.top, t->hwnd, (HMENU)1,
                           (HINSTANCE)GetWindowLongPtrW(t->hwnd, GWLP_HINSTANCE), NULL);
  if (!e) return NULL;
  SendMessageW(e, WM_SETFONT, (WPARAM)FontOf(t), FALSE);
  t->edit = e;
  t->editItem = n;
  NMTVDISPINFOW di = {};
  std::string narrow = Narrow(text.c_str());
  Describe(n, &di.item);
  di.item.mask |= TVIF_TEXT;
  di.item.pszText = t->unicodeNotify ? (LPWSTR)text.c_str() : (LPWSTR)narrow.c_str();
  if (Notify(t, Code(t, TVN_BEGINLABELEDITW, TVN_BEGINLABELEDITA), &di.hdr)) {
    t->edit = NULL;
    t->editItem = NULL;
    DestroyWindow(e);
    return NULL;
  }
  if (!IsWindow(e)) return NULL;
  SetWindowSubclass(e, EditSubclass, kEditSubclass, (DWORD_PTR)t);
  ShowWindow(e, SW_SHOW);
  SetFocus(e);
  SendMessageW(e, EM_SETSEL, 0, -1);
  return e;
}

void EndEdit(Tree* t, bool save) {
  if (!t->edit || t->ending) return;
  t->ending = true;
  HWND e = t->edit;
  Node* n = t->editItem;
  int len = GetWindowTextLengthW(e);
  std::wstring text(len + 1, L'\0');
  GetWindowTextW(e, &text[0], len + 1);
  text.resize(len);
  t->edit = NULL;
  t->editItem = NULL;
  bool hadFocus = GetFocus() == e;
  RemoveWindowSubclass(e, EditSubclass, kEditSubclass);
  ShowWindow(e, SW_HIDE);
  if (hadFocus) SetFocus(t->hwnd);
  if (n && t->all.count(n)) {
    NMTVDISPINFOW di = {};
    std::string narrow = Narrow(text.c_str());
    Describe(n, &di.item);
    di.item.mask |= TVIF_TEXT;
    di.item.pszText = save ? (t->unicodeNotify ? (LPWSTR)text.c_str() : (LPWSTR)narrow.c_str()) : NULL;
    di.item.cchTextMax = (int)text.size() + 1;
    LRESULT keep = Notify(t, Code(t, TVN_ENDLABELEDITW, TVN_ENDLABELEDITA), &di.hdr);
    if (save && keep && t->all.count(n) && !n->callback) {
      n->text = text;
      Changed(t);
    }
    if (t->all.count(n)) InvalidateItem(t, n);
  }
  DestroyWindow(e);
  t->ending = false;
}

// ---------------------------------------------------------------------
// Painting

UINT DrawStage(Tree* t, HDC dc, DWORD stage, Node* n, const RECT& rc, UINT itemState, COLORREF* text,
               COLORREF* bk) {
  NMTVCUSTOMDRAW cd = {};
  cd.nmcd.dwDrawStage = stage;
  cd.nmcd.hdc = dc;
  cd.nmcd.rc = rc;
  cd.nmcd.dwItemSpec = (DWORD_PTR)n;
  cd.nmcd.uItemState = itemState;
  cd.nmcd.lItemlParam = n ? n->param : 0;
  cd.clrText = text ? *text : 0;
  cd.clrTextBk = bk ? *bk : 0;
  cd.iLevel = n ? Depth(n) : 0;
  UINT r = (UINT)Notify(t, NM_CUSTOMDRAW, &cd.nmcd.hdr);
  if (text) *text = cd.clrText;
  if (bk) *bk = cd.clrTextBk;
  return r;
}

void DottedV(HDC dc, int x, int y0, int y1, COLORREF c) {
  for (int y = y0 + ((y0 + x) & 1); y < y1; y += 2) SetPixel(dc, x, y, c);
}

void DottedH(HDC dc, int x0, int x1, int y, COLORREF c) {
  for (int x = x0 + ((x0 + y) & 1); x < x1; x += 2) SetPixel(dc, x, y, c);
}

void DrawButton(Tree* t, HDC dc, int cx, int cy, bool open) {
  RECT b = {cx - 4, cy - 4, cx + 5, cy + 5};
  FillSolid(dc, b, BkColor(t));
  HBRUSH frame = CreateSolidBrush(GetSysColor(COLOR_GRAYTEXT));
  FrameRect(dc, &b, frame);
  DeleteObject(frame);
  COLORREF ink = GetSysColor(COLOR_WINDOWTEXT);
  for (int x = cx - 2; x <= cx + 2; x++) SetPixel(dc, x, cy, ink);
  if (!open)
    for (int y = cy - 2; y <= cy + 2; y++) SetPixel(dc, cx, y, ink);
}

void PaintRow(Tree* t, HDC dc, int row, bool focused, UINT cdFlags) {
  Node* n = Rows(t)[row];
  Rects r = ItemRects(t, n, row);
  int ind = Indent(t);
  bool lines = HasStyle(t, TVS_HASLINES), atRoot = HasStyle(t, TVS_LINESATROOT);
  bool selected = (n->state & TVIS_SELECTED) != 0;
  bool dropped = (n->state & TVIS_DROPHILITED) != 0 || (t->drop == n);
  bool shown = dropped || (selected && (focused || HasStyle(t, TVS_SHOWSELALWAYS)) && !(t->drop && t->drop != n));
  bool active = dropped || focused;
  COLORREF text = TextColor(t), bk = BkColor(t);
  if (shown) {
    text = GetSysColor(active ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
    bk = GetSysColor(active ? COLOR_HIGHLIGHT : COLOR_BTNFACE);
  }
  UINT itemState = (selected ? CDIS_SELECTED : 0) | (n == t->caret && focused ? CDIS_FOCUS : 0);
  UINT itemCd = CDRF_DODEFAULT;
  if (cdFlags & CDRF_NOTIFYITEMDRAW) {
    itemCd = DrawStage(t, dc, CDDS_ITEMPREPAINT, n, r.line, itemState, &text, &bk);
    if (itemCd & CDRF_SKIPDEFAULT) return;
  }

  // The lines: through each ancestor that has more below, then to this item.
  if (lines) {
    COLORREF lc = LineColor(t);
    int midY = (r.line.top + r.line.bottom) / 2;
    int level = Depth(n) + (atRoot ? 1 : 0);
    if (level > 0) {
      int cx = r.button.left + ind / 2;
      DottedH(dc, cx, r.button.right, midY, lc);
      bool first = !n->prev && n->parent == &t->root;
      DottedV(dc, cx, first ? midY : r.line.top, n->next ? r.line.bottom : midY + 1, lc);
      int x = cx;
      for (Node* p = n->parent; p && p != &t->root; p = p->parent) {
        x -= ind;
        if (p->next && (p->parent != &t->root || atRoot)) DottedV(dc, x, r.line.top, r.line.bottom, lc);
      }
    }
  }
  if (r.hasButton) {
    int cx = r.button.left + ind / 2, cy = (r.line.top + r.line.bottom) / 2;
    DrawButton(t, dc, cx, cy, (n->state & TVIS_EXPANDED) != 0);
  }
  // The state image or check box.
  int stateIndex = (int)((n->state & TVIS_STATEIMAGEMASK) >> 12);
  if (stateIndex && r.state.right > r.state.left) {
    if (t->state) {
      SIZE s = ImageSize(t->state);
      ImageList_Draw(t->state, stateIndex - 1, dc, r.state.left, r.state.top + (r.state.bottom - r.state.top - s.cy) / 2,
                     ILD_TRANSPARENT);
    } else {
      RECT b = {r.state.left + 1, r.state.top + (r.state.bottom - r.state.top - 13) / 2, 0, 0};
      b.right = b.left + 13;
      b.bottom = b.top + 13;
      DrawFrameControl(dc, &b, DFC_BUTTON, DFCS_BUTTONCHECK | (stateIndex == 2 ? DFCS_CHECKED : 0));
    }
  }
  Shown s = Resolve(t, n, TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE);
  if (t->normal) {
    int image = selected ? s.selImage : s.image;
    SIZE is = ImageSize(t->normal);
    UINT f = ILD_TRANSPARENT | (n->state & TVIS_OVERLAYMASK);
    if ((n->state & TVIS_CUT) || (shown && active && dropped)) f |= ILD_BLEND50;
    if (image >= 0)
      ImageList_Draw(t->normal, image, dc, r.icon.left, r.icon.top + (r.icon.bottom - r.icon.top - is.cy) / 2, f);
  }
  RECT lab = r.label;
  if (HasStyle(t, TVS_FULLROWSELECT) && !lines) lab.right = std::max(lab.right, r.line.right);
  if (shown || bk != BkColor(t)) FillSolid(dc, lab, bk);
  HGDIOBJ oldFont = NULL;
  if ((n->state & TVIS_BOLD) && !(itemCd & CDRF_NEWFONT)) oldFont = SelectObject(dc, BoldFont(t));
  SetTextColor(dc, text);
  RECT tr = r.label;
  tr.left += 2;
  if (n != t->editItem)
    DrawTextW(dc, s.text.c_str(), (int)s.text.size(), &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_LEFT);
  if (oldFont) SelectObject(dc, oldFont);
  if (n == t->caret && focused && !dropped) DrawFocusRect(dc, &lab);
  if (n == t->insertMark) {
    int y = t->insertAfter ? r.line.bottom - 1 : r.line.top;
    COLORREF mc = t->markColor == CLR_DEFAULT ? GetSysColor(COLOR_WINDOWTEXT) : t->markColor;
    RECT mr = {r.icon.left, y - 1, r.label.right, y + 1};
    FillSolid(dc, mr, mc);
  }
  if (itemCd & CDRF_NOTIFYPOSTPAINT) DrawStage(t, dc, CDDS_ITEMPOSTPAINT, n, r.line, itemState, NULL, NULL);
}

void Paint(Tree* t, HDC dc, const RECT& update) {
  RECT c;
  GetClientRect(t->hwnd, &c);
  HGDIOBJ oldFont = SelectObject(dc, FontOf(t));
  SetBkMode(dc, TRANSPARENT);
  UINT cd = DrawStage(t, dc, CDDS_PREPAINT, NULL, c, 0, NULL, NULL);
  if (!(cd & CDRF_SKIPDEFAULT)) {
    int h = ItemHeight(t);
    int first = t->top + std::max(0, (int)update.top / h), last = t->top + update.bottom / h;
    bool focused = HasFocus(t);
    int n = (int)Rows(t).size();
    for (int row = first; row <= last && row < n; row++) PaintRow(t, dc, row, focused, cd);
    if (cd & CDRF_NOTIFYPOSTPAINT) DrawStage(t, dc, CDDS_POSTPAINT, NULL, c, 0, NULL, NULL);
  }
  SelectObject(dc, oldFont);
}

HIMAGELIST CreateDragImage(Tree* t, Node* n) {
  int row = RowOf(t, n);
  if (!n || row < 0) return NULL;
  Rects r = ItemRects(t, n, row);
  int x0 = r.icon.left, w = std::max(1, (int)(r.label.right - x0)), h = r.line.bottom - r.line.top;
  HDC screen = GetDC(NULL);
  HDC mem = CreateCompatibleDC(screen);
  HBITMAP bm = CreateCompatibleBitmap(screen, w, h);
  HGDIOBJ old = SelectObject(mem, bm);
  COLORREF key = RGB(255, 0, 255);
  RECT all = {0, 0, w, h};
  FillSolid(mem, all, key);
  Shown s = Resolve(t, n, TVIF_TEXT | TVIF_IMAGE);
  if (t->normal && s.image >= 0) {
    SIZE is = ImageSize(t->normal);
    ImageList_Draw(t->normal, s.image, mem, 0, (h - is.cy) / 2, ILD_TRANSPARENT);
  }
  HGDIOBJ oldFont = SelectObject(mem, FontOf(t));
  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, TextColor(t));
  RECT tr = {r.label.left - x0 + 2, 0, w, h};
  DrawTextW(mem, s.text.c_str(), (int)s.text.size(), &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  SelectObject(mem, oldFont);
  SelectObject(mem, old);
  HIMAGELIST drag = ImageList_Create(w, h, ILC_COLOR24 | ILC_MASK, 1, 0);
  ImageList_AddMasked(drag, bm, key);
  DeleteObject(bm);
  DeleteDC(mem);
  ReleaseDC(NULL, screen);
  return drag;
}

// ---------------------------------------------------------------------
// Keyboard and mouse

void KeyDown(Tree* t, WPARAM vk) {
  NMTVKEYDOWN kd = {};
  kd.wVKey = (WORD)vk;
  Notify(t, TVN_KEYDOWN, &kd.hdr);
  std::vector<Node*>& rows = Rows(t);
  if (rows.empty()) return;
  Node* cur = t->caret;
  Node* to = NULL;
  int per = std::max(1, VisibleCount(t));
  switch (vk) {
    case VK_UP: to = cur ? PrevVisible(t, cur) : rows[0]; break;
    case VK_DOWN: to = cur ? NextVisible(t, cur) : rows[0]; break;
    case VK_HOME: to = rows[0]; break;
    case VK_END: to = rows.back(); break;
    case VK_PRIOR:
    case VK_NEXT: {
      int r = cur ? RowOf(t, cur) : 0;
      r = vk == VK_NEXT ? std::min((int)rows.size() - 1, r + per - 1) : std::max(0, r - per + 1);
      to = rows[r];
      break;
    }
    case VK_LEFT:
      if (!cur) return;
      if ((cur->state & TVIS_EXPANDED) && HasChildren(t, cur))
        Expand(t, cur, TVE_COLLAPSE, true);
      else if (cur->parent != &t->root)
        to = cur->parent;
      break;
    case VK_BACK:
      if (cur && cur->parent != &t->root) to = cur->parent;
      break;
    case VK_RIGHT:
      if (!cur) return;
      if (!(cur->state & TVIS_EXPANDED) && HasChildren(t, cur))
        Expand(t, cur, TVE_EXPAND, true);
      else if (cur->first && (cur->state & TVIS_EXPANDED))
        to = cur->first;
      break;
    case VK_ADD:
      if (cur) Expand(t, cur, TVE_EXPAND, true);
      return;
    case VK_SUBTRACT:
      if (cur) Expand(t, cur, TVE_COLLAPSE, true);
      return;
    case VK_MULTIPLY:
      if (cur) ExpandAll(t, cur);
      return;
    case VK_SPACE:
      if (cur && HasStyle(t, TVS_CHECKBOXES)) ToggleCheck(t, cur);
      return;
    case VK_RETURN: Notify(t, NM_RETURN); return;
    case VK_F2:
      if (cur && HasStyle(t, TVS_EDITLABELS)) BeginEdit(t, cur);
      return;
    default: return;
  }
  if (to) Select(t, to, TVC_BYKEYBOARD);
}

void TypeAhead(Tree* t, wchar_t ch) {
  if (ch < L' ') return;
  DWORD now = GetTickCount();
  if (now - t->searchTime > 1000) t->search.clear();
  t->searchTime = now;
  t->search += ch;
  std::vector<Node*> rows = Rows(t);
  int n = (int)rows.size();
  if (!n) return;
  bool cycle = t->search.size() > 1 && std::all_of(t->search.begin(), t->search.end(), [&](wchar_t c) {
                 return towlower(c) == towlower(t->search[0]);
               });
  std::wstring want = cycle ? t->search.substr(0, 1) : t->search;
  int start = t->caret ? RowOf(t, t->caret) : -1;
  if (t->search.size() > 1 && !cycle) start--;
  for (int k = 1; k <= n; k++) {
    Node* c = rows[((start + k) % n + n) % n];
    std::wstring s = TextOf(t, c);
    if (s.size() >= want.size() && _wcsnicmp(s.c_str(), want.c_str(), want.size()) == 0) {
      Select(t, c, TVC_BYKEYBOARD);
      return;
    }
  }
}

bool OnItem(Tree* t, UINT flags) {
  if (flags & (TVHT_ONITEMICON | TVHT_ONITEMLABEL | TVHT_ONITEMSTATEICON)) return true;
  return HasStyle(t, TVS_FULLROWSELECT) && (flags & (TVHT_ONITEMINDENT | TVHT_ONITEMRIGHT));
}

void ButtonDown(Tree* t, POINT pt, bool right, bool dbl) {
  if (t->edit) EndEdit(t, true);
  KillTimer(t->hwnd, kEditTimer);
  t->pendingEdit = NULL;
  if (GetFocus() != t->hwnd) SetFocus(t->hwnd);
  UINT flags;
  Node* n = HitTest(t, pt, &flags);
  if (dbl) {
    if (right) {
      Notify(t, NM_RDBLCLK);
      return;
    }
    if (n && (flags & TVHT_ONITEMBUTTON)) {
      Expand(t, n, TVE_TOGGLE, true);
      return;
    }
    if (!Notify(t, NM_DBLCLK) && n && OnItem(t, flags)) Expand(t, n, TVE_TOGGLE, true);
    return;
  }
  if (!right && n && (flags & TVHT_ONITEMBUTTON)) {
    Expand(t, n, TVE_TOGGLE, true);
    return;
  }
  if (!right && n && (flags & TVHT_ONITEMSTATEICON) && HasStyle(t, TVS_CHECKBOXES)) ToggleCheck(t, n);
  t->pressItem = n && OnItem(t, flags) ? n : NULL;
  t->pressPt = pt;
  t->pressRight = right;
  t->pressWasCaret = n && n == t->caret;
  t->pressOnLabel = (flags & TVHT_ONITEMLABEL) != 0;
  t->tracking = true;
  SetCapture(t->hwnd);
  if (right && t->pressItem) SetDrop(t, t->pressItem);
}

void ButtonUp(Tree* t, POINT pt, bool right) {
  if (!t->tracking || t->pressRight != right) return;
  t->tracking = false;
  if (GetCapture() == t->hwnd) ReleaseCapture();
  Node* n = t->pressItem;
  if (right) {
    if (n) SetDrop(t, NULL);
    if (!Notify(t, NM_RCLICK)) {
      POINT s = pt;
      ClientToScreen(t->hwnd, &s);
      SendMessageW(t->hwnd, WM_CONTEXTMENU, (WPARAM)t->hwnd, MAKELPARAM(s.x, s.y));
    }
    return;
  }
  if (Notify(t, NM_CLICK)) return;
  if (n && t->all.count(n)) {
    Select(t, n, TVC_BYMOUSE);
    if (t->pressWasCaret && t->pressOnLabel && HasStyle(t, TVS_EDITLABELS)) {
      t->pendingEdit = n;
      SetTimer(t->hwnd, kEditTimer, GetDoubleClickTime(), NULL);
    }
  }
}

void MouseMove(Tree* t, POINT pt) {
  if (!t->tracking || !t->pressItem) return;
  if (abs(pt.x - t->pressPt.x) < GetSystemMetrics(SM_CXDRAG) && abs(pt.y - t->pressPt.y) < GetSystemMetrics(SM_CYDRAG))
    return;
  Node* n = t->pressItem;
  bool right = t->pressRight;
  t->tracking = false;
  if (GetCapture() == t->hwnd) ReleaseCapture();
  if (right) SetDrop(t, NULL);
  if (HasStyle(t, TVS_DISABLEDRAGDROP)) return;
  if (right)
    NotifyTree(t, TVN_BEGINRDRAGW, TVN_BEGINRDRAGA, 0, NULL, n, t->pressPt);
  else
    NotifyTree(t, TVN_BEGINDRAGW, TVN_BEGINDRAGA, 0, NULL, n, t->pressPt);
}

void Scroll(Tree* t, int bar, int code) {
  SCROLLINFO si = {sizeof si, SIF_ALL};
  GetScrollInfo(t->hwnd, bar, &si);
  int pos = si.nPos, page = std::max(1, (int)si.nPage), line = bar == SB_VERT ? 1 : FontHeight(t);
  switch (code) {
    case SB_LINEUP: pos -= line; break;
    case SB_LINEDOWN: pos += line; break;
    case SB_PAGEUP: pos -= page; break;
    case SB_PAGEDOWN: pos += page; break;
    case SB_TOP: pos = si.nMin; break;
    case SB_BOTTOM: pos = si.nMax; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    default: return;
  }
  pos = std::max(si.nMin, std::min(pos, si.nMax - std::max(0, (int)si.nPage - 1)));
  if (bar == SB_VERT)
    t->top = pos;
  else
    t->scrollX = pos;
  UpdateScroll(t);
  InvalidateRect(t->hwnd, NULL, TRUE);
}

// ---------------------------------------------------------------------
// The window procedure

LRESULT CALLBACK TreeProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Tree* t = StateOf(h);
  if (msg == WM_NCCREATE) {
    t = new Tree;
    t->hwnd = h;
    SetWindowLongPtrW(h, 0, (LONG_PTR)t);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!t) return DefWindowProcW(h, msg, wp, lp);
  bool u = true;
  switch (msg) {
    case WM_CREATE:
      t->unicodeNotify = NotifyUnicode(h);
      UpdateScroll(t);
      return 0;
    case WM_DESTROY:
      if (t->edit) EndEdit(t, false);
      DeleteTree(t, &t->root);
      return 0;
    case WM_NCDESTROY:
      if (t->bold) DeleteObject(t->bold);
      delete t;
      SetWindowLongPtrW(h, 0, 0);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_NOTIFYFORMAT:
      if (lp == NF_REQUERY) t->unicodeNotify = NotifyUnicode(h);
      return t->unicodeNotify ? NFR_UNICODE : NFR_ANSI;
    case WM_STYLECHANGED:
      if (wp == (WPARAM)GWL_STYLE) {
        STYLESTRUCT* ss = (STYLESTRUCT*)lp;
        if ((ss->styleNew & TVS_CHECKBOXES) && !(ss->styleOld & TVS_CHECKBOXES))
          for (HTREEITEM i : t->all) {
            Node* n = static_cast<Node*>(i);
            if (!(n->state & TVIS_STATEIMAGEMASK)) n->state |= INDEXTOSTATEIMAGEMASK(1);
          }
        Changed(t);
      }
      return 0;
    case WM_SIZE:
      UpdateScroll(t);
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_SETFONT:
      t->font = (HFONT)wp;
      if (t->bold) DeleteObject(t->bold), t->bold = NULL;
      UpdateScroll(t);
      if (LOWORD(lp)) InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETFONT: return (LRESULT)t->font;
    case WM_ERASEBKGND: {
      RECT r;
      GetClientRect(h, &r);
      FillSolid((HDC)wp, r, BkColor(t));
      return 1;
    }
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      if (msg == WM_PRINTCLIENT || wp) {
        RECT r;
        GetClientRect(h, &r);
        if (msg == WM_PRINTCLIENT) FillSolid((HDC)wp, r, BkColor(t));
        Paint(t, (HDC)wp, r);
        return 0;
      }
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      Paint(t, dc, ps.rcPaint);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      Notify(t, msg == WM_SETFOCUS ? NM_SETFOCUS : NM_KILLFOCUS);
      if (t->caret) InvalidateItem(t, t->caret);
      return 0;
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
      KeyDown(t, wp);
      return 0;
    case WM_CHAR:
      TypeAhead(t, (wchar_t)wp);
      return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
      ButtonDown(t, POINT{(short)LOWORD(lp), (short)HIWORD(lp)}, msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK,
                 msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK);
      return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
      ButtonUp(t, POINT{(short)LOWORD(lp), (short)HIWORD(lp)}, msg == WM_RBUTTONUP);
      return 0;
    case WM_MOUSEMOVE:
      MouseMove(t, POINT{(short)LOWORD(lp), (short)HIWORD(lp)});
      return 0;
    case WM_CAPTURECHANGED:
      if ((HWND)lp != h) {
        t->tracking = false;
        if (t->pressRight && t->drop == t->pressItem) SetDrop(t, NULL);
      }
      return 0;
    case WM_TIMER:
      if (wp == kEditTimer) {
        KillTimer(h, kEditTimer);
        Node* n = t->pendingEdit;
        t->pendingEdit = NULL;
        if (n && n == t->caret && HasFocus(t)) BeginEdit(t, n);
        return 0;
      }
      break;
    case WM_VSCROLL:
    case WM_HSCROLL:
      if (t->edit) EndEdit(t, true);
      Scroll(t, msg == WM_VSCROLL ? SB_VERT : SB_HORZ, LOWORD(wp));
      return 0;
    case WM_MOUSEWHEEL: {
      int lines = -(short)HIWORD(wp) / WHEEL_DELTA * 3;
      for (int k = 0; k < abs(lines); k++) Scroll(t, SB_VERT, lines > 0 ? SB_LINEDOWN : SB_LINEUP);
      return 0;
    }
    case WM_COMMAND: return 0;

    case CCM_SETUNICODEFORMAT: {
      bool old = t->unicodeNotify;
      t->unicodeNotify = wp != 0;
      return old;
    }
    case CCM_GETUNICODEFORMAT: return t->unicodeNotify;

    case TVM_INSERTITEMA: u = false; [[fallthrough]];
    case TVM_INSERTITEMW: return (LRESULT)InsertItem(t, (const TVINSERTSTRUCTW*)lp, u);
    case TVM_DELETEITEM: return DeleteItem(t, (HTREEITEM)lp);
    case TVM_EXPAND: return Expand(t, Valid(t, (HTREEITEM)lp), (UINT)wp, false);
    case TVM_GETITEMRECT: {
      RECT* out = (RECT*)lp;
      if (!out) return FALSE;
      Node* n = Valid(t, *(HTREEITEM*)lp);
      int row = n ? RowOf(t, n) : -1;
      if (row < 0) return FALSE;
      Rects r = ItemRects(t, n, row);
      *out = wp ? r.label : r.line;
      return TRUE;
    }
    case TVM_GETCOUNT: return (LRESULT)t->all.size();
    case TVM_GETINDENT: return Indent(t);
    case TVM_SETINDENT:
      t->indent = std::max(5, (int)wp);
      Changed(t);
      return 0;
    case TVM_GETIMAGELIST: return (LRESULT)(wp == TVSIL_STATE ? t->state : wp == TVSIL_NORMAL ? t->normal : NULL);
    case TVM_SETIMAGELIST: {
      HIMAGELIST old;
      if (wp == TVSIL_STATE) {
        old = t->state;
        t->state = (HIMAGELIST)lp;
      } else if (wp == TVSIL_NORMAL) {
        old = t->normal;
        t->normal = (HIMAGELIST)lp;
      } else {
        return 0;
      }
      Changed(t);
      return (LRESULT)old;
    }
    case TVM_GETNEXTITEM: return (LRESULT)NextItem(t, (HTREEITEM)lp, (UINT)wp);
    case TVM_SELECTITEM: {
      Node* n = lp ? Valid(t, (HTREEITEM)lp) : NULL;
      if (lp && !n) return FALSE;
      switch (wp & 0xFF) {
        case TVGN_CARET: return Select(t, n, TVC_UNKNOWN);
        case TVGN_DROPHILITE:
          SetDrop(t, n);
          return TRUE;
        case TVGN_FIRSTVISIBLE: {
          if (!n) return FALSE;
          EnsureVisible(t, n);
          int row = RowOf(t, n);
          if (row >= 0) ScrollToRow(t, row);
          return TRUE;
        }
      }
      return FALSE;
    }
    case TVM_GETITEMA: u = false; [[fallthrough]];
    case TVM_GETITEMW: return GetItem(t, (TVITEMEXW*)lp, u);
    case TVM_SETITEMA: u = false; [[fallthrough]];
    case TVM_SETITEMW: return SetItem(t, (const TVITEMEXW*)lp, u);
    case TVM_EDITLABELA:
    case TVM_EDITLABELW: return (LRESULT)BeginEdit(t, Valid(t, (HTREEITEM)lp));
    case TVM_GETEDITCONTROL: return (LRESULT)t->edit;
    case TVM_ENDEDITLABELNOW:
      if (!t->edit) return FALSE;
      EndEdit(t, wp == 0);
      return TRUE;
    case TVM_GETVISIBLECOUNT: return VisibleCount(t);
    case TVM_HITTEST: {
      TVHITTESTINFO* ht = (TVHITTESTINFO*)lp;
      if (!ht) return 0;
      ht->hItem = HitTest(t, ht->pt, &ht->flags);
      return (LRESULT)ht->hItem;
    }
    case TVM_CREATEDRAGIMAGE: return (LRESULT)CreateDragImage(t, Valid(t, (HTREEITEM)lp));
    case TVM_SORTCHILDREN: {
      Node* p = ParentFor(t, (HTREEITEM)lp);
      if (!p) return FALSE;
      SortChildren(t, p, wp != 0, [&](Node* a, Node* b) { return _wcsicmp(TextOf(t, a).c_str(), TextOf(t, b).c_str()) < 0; });
      Changed(t);
      return TRUE;
    }
    case TVM_SORTCHILDRENCB: {
      TVSORTCB* sc = (TVSORTCB*)lp;
      if (!sc || !sc->lpfnCompare) return FALSE;
      Node* p = ParentFor(t, sc->hParent);
      if (!p) return FALSE;
      SortChildren(t, p, wp != 0, [&](Node* a, Node* b) { return sc->lpfnCompare(a->param, b->param, sc->lParam) < 0; });
      Changed(t);
      return TRUE;
    }
    case TVM_ENSUREVISIBLE: return EnsureVisible(t, Valid(t, (HTREEITEM)lp));
    case TVM_GETISEARCHSTRINGA: u = false; [[fallthrough]];
    case TVM_GETISEARCHSTRINGW:
      if (GetTickCount() - t->searchTime > 1000) t->search.clear();
      if (lp) CopyOut(t->search, (void*)lp, (int)t->search.size() + 1, u);
      return (LRESULT)t->search.size();
    case TVM_SETTOOLTIPS: {
      HWND old = t->tooltips;
      t->tooltips = (HWND)wp;
      return (LRESULT)old;
    }
    case TVM_GETTOOLTIPS: return (LRESULT)t->tooltips;
    case TVM_SETINSERTMARK: {
      if (t->insertMark && t->all.count(t->insertMark)) InvalidateItem(t, t->insertMark);
      t->insertMark = Valid(t, (HTREEITEM)lp);
      t->insertAfter = wp != 0;
      if (t->insertMark) InvalidateItem(t, t->insertMark);
      return TRUE;
    }
    case TVM_SETITEMHEIGHT: {
      int old = ItemHeight(t);
      int nh = (short)LOWORD(wp);
      if (nh == -1 || nh <= 0) {
        t->itemHeight = 0;
      } else {
        if (!HasStyle(t, TVS_NONEVENHEIGHT) && (nh & 1)) nh--;
        t->itemHeight = std::max(1, nh);
      }
      Changed(t);
      return old;
    }
    case TVM_GETITEMHEIGHT: return ItemHeight(t);
    case TVM_SETBKCOLOR: {
      COLORREF old = t->bk == CLR_DEFAULT ? (COLORREF)-1 : t->bk;
      t->bk = lp == -1 ? CLR_DEFAULT : (COLORREF)lp;
      InvalidateRect(h, NULL, TRUE);
      return old;
    }
    case TVM_GETBKCOLOR: return t->bk == CLR_DEFAULT ? (LRESULT)-1 : t->bk;
    case TVM_SETTEXTCOLOR: {
      COLORREF old = t->text == CLR_DEFAULT ? (COLORREF)-1 : t->text;
      t->text = lp == -1 ? CLR_DEFAULT : (COLORREF)lp;
      InvalidateRect(h, NULL, TRUE);
      return old;
    }
    case TVM_GETTEXTCOLOR: return t->text == CLR_DEFAULT ? (LRESULT)-1 : t->text;
    case TVM_SETLINECOLOR: {
      COLORREF old = LineColor(t);
      t->line = (COLORREF)lp;
      InvalidateRect(h, NULL, TRUE);
      return old;
    }
    case TVM_GETLINECOLOR: return LineColor(t);
    case TVM_SETINSERTMARKCOLOR: {
      COLORREF old = t->markColor;
      t->markColor = (COLORREF)lp;
      return old;
    }
    case TVM_GETINSERTMARKCOLOR: return t->markColor;
    case TVM_SETSCROLLTIME: {
      UINT old = t->scrollTime;
      t->scrollTime = (UINT)wp;
      return old;
    }
    case TVM_GETSCROLLTIME: return t->scrollTime;
    case TVM_GETITEMSTATE: {
      Node* n = Valid(t, (HTREEITEM)wp);
      return n ? (n->state & (UINT)lp) : 0;
    }
  }
  return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace winport {

void RegisterTreeView() {
  RegisterSystemClass(WC_TREEVIEWW, TreeProc, CS_DBLCLKS | CS_GLOBALCLASS, sizeof(void*), NULL,
                      MAKEINTRESOURCEW(32512));
}

}  // namespace winport
