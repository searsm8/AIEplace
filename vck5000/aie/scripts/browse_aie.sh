#!/usr/bin/env bash

DIR="$1"
TMPVIMRC=$(mktemp)

cat > "$TMPVIMRC" << 'EOF'
set nocompatible
set noswapfile
set nowrap
set hidden
set nobackup
set nowritebackup

" ---------- Tree on the left ----------
let g:netrw_banner=0
let g:netrw_liststyle=3
let g:netrw_keepdir=0
let g:netrw_winsize=50
let g:netrw_browse_split=4
let g:netrw_altv=1

" ---------- Hide unwanted files/folders ----------
let g:netrw_list_hide='\v(^scripts$|^timestamped_log$|\.o$|\.o\.lst$|\.log$|\.json$|\.txt$|\.cmic.|\.srv$|\.sdr$|\.prx$|\.bcf$|^\d+_\d+$|^\d+_\d+\.#$|^\d+_\d+\.##$)'

" ---------- Read-only everywhere ----------
autocmd BufEnter * setlocal readonly nomodifiable

" ---------- Right buffer: line numbers ----------
augroup AIEFileView
  autocmd!
  autocmd BufEnter * if &filetype !=# 'netrw' |
        \ setlocal number relativenumber |
        \ endif
augroup END

" ---------- Disable insert ----------
nnoremap i <Nop>
nnoremap a <Nop>
nnoremap I <Nop>
nnoremap A <Nop>

" ---------- Quit all ----------
nnoremap q :qa!<CR>

" ---------- Setup netrw tree ----------
augroup AIEBrowser
  autocmd!
  autocmd FileType netrw call s:SetupTree()
augroup END

function! s:SetupTree()
  vertical resize 50
  setlocal winfixwidth
  setlocal nonumber norelativenumber
endfunction

" ---------- Sticky instructions via statusline ----------
set laststatus=2
" Correctly escape pipes and spaces
set statusline=%#ModeMsg#Instructions:\ q=quit\ \|\ Ctrl-W←/→=switch\ \|\ Enter=open%*
EOF

# Launch Vim
vim -u "$TMPVIMRC" -n -c "Vexplore $DIR"

rm "$TMPVIMRC"
