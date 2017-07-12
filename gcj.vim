let s:bin = "/home/chenkan/work/gcc-jump/src/gcj"
let s:db = "/home/chenkan/work/gcc-jump/test/.test/db"
let s:ctx = "/home/chenkan/work/gcc-jump/test/.test/ctx"
"let s:db = "/home/chenkan/work/gcc-jump/test/.run.binutils-gdb/db"
"let s:ctx = "/home/chenkan/work/gcc-jump/test/.run.binutils-gdb/ctx"
"let s:db = "/home/chenkan/work/gcc-jump/test/.run.glibc/db"
"let s:ctx = "/home/chenkan/work/gcc-jump/test/.run.glibc/ctx"

call system("mkdir -p " . s:ctx)

function s:Gcj(command)
  let cmd = s:bin . " " . s:db . " " . a:command
  echom cmd
  return system(cmd . " 2> /dev/null")
endfunction

function s:FindExpWin(id)
  for winnr in range(1, winnr("$"))
    if getwinvar(winnr, "gcj_exp_win_id") == a:id
      return winnr
    endif
  endfor
  return -1
endfunction

function s:SetExpTok(id)

  let layout = b:gcj_expansion.layout
  for [ tokcol, tokid ] in layout
    if tokid == a:id
      let col = tokcol
      break
    endif
  endfor

  call cursor(1, col)
endfunction

function s:GetExpTok()

  if line(".") != 1
    return 0
  endif

  let col = col(".")
  let layout = b:gcj_expansion.layout
  for i in range(1, len(layout))
    if layout[i][0] > col
      if layout[i][0] - 1 == col
        return 0
      endif
      return layout[i - 1][1]
    endif
  endfor

  return layout[-1][1]
endfunction

function s:BufName()
  return expand('%:p')
endfunction

function s:SetContext(edit, filename, context)

  let filename = a:filename
  if filename[0] != "/"
    let filename = fnamemodify(filename, ":p")
  endif

  call system("mkdir -p " . s:ctx . fnamemodify(filename, ":p:h"))
  let sctx = a:context.unit . "." . a:context.include . "." . a:context.point
  let ext = fnamemodify(filename, ":e")
  let link = s:ctx . fnamemodify(filename, ":r") . "." . sctx . "." . ext
  " Using cp instead of a real link because vim can't handle well with the different
  " file names pointing to the same inode
  call system("rm -f " . link)
  call system("cp " . fnamemodify(filename, ":p") . " " . link)
  call system("echo -n '[ " . a:context.unit . ", " . a:context.include . ", " . a:context.point . "]' > " . link . ".gcj.ctx")
  execute a:edit . " " . link
endfunction

function s:HasContext()
  return filereadable(s:BufName() . ".gcj.ctx")
endfunction

function s:GetContext()
  let ctx = eval(system("cat " . s:BufName() . ".gcj.ctx"))
  return { "unit": ctx[0], "include": ctx[1], "point": ctx[2] }
endfunction

function s:Jump()

  if !s:HasContext() && !exists("b:gcj_expansion")
    echom "Not in gcj context"
    return
  endif

  let exptok = 0
  if exists("b:gcj_expansion")
    let exp = b:gcj_expansion
    let ctx = exp.context
    let pos = exp.position
    let expid = s:GetExpTok()
    if expid == 0
      return
    endif
  else
    let ctx = s:GetContext()
    let pos = { 'line': line("."), 'col': col(".") }
    let expid = 0
  endif

  let sctx = ctx.unit . " " . ctx.include . " " . ctx.point
  let spos = pos.line . " " . pos.col . " " . expid
  let sjmp = s:Gcj("jump " . sctx . " " . spos)

  if sjmp == ''
    return
  endif

  let [ filename, context, newpos ] = eval(sjmp)
  if exists("exp")
    let winnr = s:FindExpWin(exp.parent)
    if winnr != -1
      execute winnr . "wincmd w"
    endif
  endif

  call s:SetContext("edit", filename, context)
  call cursor(newpos.line, newpos.col)

  if newpos.expid != 0
    call s:Expand()
    call s:SetExpTok(newpos.expid)
  endif

endfunction

function s:GetExpWin()
  let id = w:gcj_exp_win_id
  let orig_winnr = winnr()
  for winnr in range(1, winnr("$"))
    execute winnr . " wincmd w"
    if exists("b:gcj_expansion") && b:gcj_expansion.parent == id
      return
    endif
  endfor
  execute orig_winnr . " wincmd w"
  below 5new
endfunction

let s:exp_win_id = 0
function s:Expand()

  if !s:HasContext()
    echom "Not in gcj context"
    return
  endif

  let ctx = s:GetContext()
  let sctx = ctx.unit . " " . ctx.include . " " . ctx.point
  let spos = line(".") . " " . col(".")
  let sexp = s:Gcj("expand " . sctx . " " . spos)

  if sexp == ''
    return
  endif

  let [ pos, tokids ] = eval(sexp)
  if len(tokids) == 0
    return
  endif

  let tokcol = 1
  let toks = [ ]
  let layout = [ ]
  for [ tok, id ] in tokids
    call add(layout, [ tokcol, id ])
    let tokcol += len(tok) + 1
    call add(toks, tok)
  endfor
  let stoks = join(toks)

  if !exists("w:gcj_exp_win_id")
    let s:exp_win_id += 1
    let w:gcj_exp_win_id = s:exp_win_id
  endif

  let exp = { "context": ctx, "position": pos, "layout": layout, "parent": w:gcj_exp_win_id }
  call s:GetExpWin()
  setlocal modifiable
  let b:gcj_expansion = exp
  call setline(1, stoks)
  set buftype=nofile
  set syntax=c
  setlocal nomodifiable
endfunction

function s:SelectUnit()

  let sel = s:Gcj("select_unit " . b:gcj_units[line(".") - 1][1])

  if sel == ''
    echom "Gcj unit not found in database " . s:db
    return
  endif

  let [ filename, context ] = eval(sel)

  for winnr in range(1, winnr('$'))

    let bufnr = winbufnr(winnr)

    execute winnr . "wincmd w"
    if !exists("b:gcj_units") && (&mod == 0 || bufwinnr(bufnr) != winnr)
      call s:SetContext("edit", filename, context)
      return
    endif

  endfor

  let maxnr = 1
  let maxarea = winwidth(1) * winheight(1)

  for winnr in range(2, winnr('$'))

    let area = winwidth(winnr) * winheight(winnr)
    if area > maxarea
      let maxnr = winnr
      let maxarea = area
    endif

  endfor

  execute maxnr . "wincmd w"
  let height = max([ 0, winheight(0) - 5 ])
  call s:SetContext("below " . height . "split", filename, context)

endfunction

function s:SetObject(...)

  if a:0 == 0
    let units = eval(s:Gcj("list_elf"))
  elseif a:0 == 1

    let name = a:1
    let units = eval(s:Gcj("list_elf " . name))
    if v:shell_error == 1
      echom "Invalid object file " . name
      return
    endif

  else
    echom "Invalid number of arguments"
    return
  endif

  if len(units) == 0
    echom "No gcj unit found in object file" . a:name
    return
  endif

  if !exists("b:gcj_units")
    5new
  else
    enew
  endif

  set winfixheight
  set buftype=nofile
  setlocal nowrap
  nnoremap <buffer> <CR> :call <SID>SelectUnit()<CR>

  sort(units)
  let b:gcj_units = units

  for i in range(len(b:gcj_units))
    call setline(i + 1, b:gcj_units[i][0])
  endfor

  setlocal nomodifiable

endfunction

command -nargs=* -complete=file GcjObj call s:SetObject(<q-args>)
nnoremap <leader>j :call <SID>Jump()<CR>
nnoremap <leader>e :call <SID>Expand()<CR>
nnoremap <leader>o :call <SID>Older()<CR>
nnoremap <leader>i :call <SID>Newer()<CR>
