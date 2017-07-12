#include <limits.h>
#include <stdlib.h>

#include <sstream>

#include "gcj.hpp"

std::string
escape (const char *str, char c)
{
  std::string ret;
  for (const char *p = str; *p; ++ p)
    {
      if (*p == '\\' || *p == c)
	ret += '\\';
      ret += *p;
    }
  return ret;
}

namespace gcj
{

static std::string
tostr (int v)
{
  std::ostringstream oss;
  oss << v;
  return oss.str ();
}

static std::string
joinpath (const char *first, ...)
{
  std::string p (first);
  va_list ap;
  va_start (ap, first);
  const char *n;
  while (n = va_arg (ap, char *))
   p = p + '/' + n;
  va_end (ap);

  return p;
}

static void
save_string (FILE *fp, const std::string& str)
{
  save_int32 (fp, str.size ());
  assert (fwrite (str.c_str (), 1, str.size (), fp) == str.size ());
}

static void
load_string (FILE *fp, std::string *str)
{
  int size;
  load_int32 (fp, &size);
  char buf[size];
  assert ((int) fread (buf, 1, size, fp) == size);
  *str = std::string (buf, size);
}

static void
iprintf (FILE *fp, int i, const char *fmt, ...)
{
  for (int n = 0; n < i; ++ n)
    fprintf (fp, "  ");

  va_list ap;
  va_start (ap, fmt);
  vfprintf (fp, fmt, ap);
  va_end (ap);
}

static void
save_file_location (FILE *fp, const file_location& loc)
{
  save_int32 (fp, loc.line);
  save_int32 (fp, loc.col);
}

static void
load_file_location (FILE *fp, file_location *loc)
{
  load_int32 (fp, &loc->line);
  load_int32 (fp, &loc->col);
}

static void
save_source_location (FILE *fp, const source_location& loc)
{
  save_int32 (fp, loc.fid);
  save_file_location (fp, loc.loc);
}

static void
load_source_location (FILE *fp, source_location *loc)
{
  load_int32 (fp, &loc->fid);
  load_file_location (fp, &loc->loc);
}

static void
save_source_stack (FILE *fp, const source_stack& stack)
{
  save_int32 (fp, stack.locs.size ());
  std::vector<source_location>::const_iterator it;
  for (it = stack.locs.begin ();
       it != stack.locs.end ();
       ++ it)
    save_source_location (fp, *it);
}

static void
load_source_stack (FILE *fp, source_stack *stack)
{
  int size;
  load_int32 (fp, &size);
  for (int i = 0; i < size; ++ i)
    {
      source_location loc;
      load_source_location (fp, &loc);
      stack->locs.push_back (loc);
    }
}

static void
save_expansion_point (FILE *fp, const expansion_point& point)
{
  save_int32 (fp, point.include);
  save_source_location (fp, point.loc);
}

static void
load_expansion_point (FILE *fp, expansion_point *point)
{
  load_int32 (fp, &point->include);
  load_source_location (fp, &point->loc);
}

const jump_to *
context::jump (const unit *unit,
	       const file_location& loc, int expanded_id,
	       file_location *begin) const
{
  jump_from from (loc, 0, expanded_id);
  std::map<jump_from, jump_to>::const_iterator it;
  it = jumps.upper_bound (from);
  if (it == jumps.begin ())
    goto search_surrounding;
  -- it;

  if (expanded_id != 0
      && (it->first.loc != loc || it->first.expanded_id != expanded_id))
    goto search_surrounding;

  if (expanded_id == 0 && it->first.expanded_id != 0)
    {
      it = jumps.upper_bound (jump_from (it->first.loc, 0, 0));
      if (it == jumps.begin ())
	goto search_surrounding;
      -- it;
    }

  if (expanded_id == 0
      && (it->first.loc.line != loc.line
	  || (it->first.loc.col
	      && it->first.loc.col + it->first.len <= loc.col)))
    goto search_surrounding;

  if (expanded_id == 0 && begin)
    *begin = it->first.loc;
  return &it->second;

search_surrounding:
  if (surrounding)
    return unit->get (surrounding)->jump (unit, loc, expanded_id, begin);
  else
    return NULL;
}

static void
print_jump_src (FILE *fp, const jump_from& from)
{
  fprintf (fp, "line,col: %d,%d %s:%d",
	   from.loc.line, from.loc.col,
	   from.len || ! from.loc.col ? "len" : "exp",
	   from.len || ! from.loc.col ? from.len : from.expanded_id);
}

static void
print_jump_to (FILE *fp, const jump_to& to)
{
  if (to.point)
    fprintf (fp, "expansion context: %d.%d",
	     to.include, to.point);
  else
    fprintf (fp, "context: %d", to.include);
  fprintf (fp, " line,col: %d,%d",
	   to.loc.line, to.loc.col);
  if (to.expanded_id != 0)
    fprintf (fp, " exp: %d", to.expanded_id);
}

void
context::dump (FILE *fp, int indet, const unit *unit) const
{
  iprintf (fp, indet, "jumps:\n");
  std::map<jump_from, jump_to>::const_iterator jump;
  for (jump = jumps.begin (); jump != jumps.end (); ++ jump)
    {
      iprintf (fp, indet + 1, "");
      print_jump_src (fp, jump->first);

      if (jump->second.include)
	{
	  fprintf (fp, " => ");
	  print_jump_to (fp, jump->second);
	}

      fprintf (fp, "\n");

      if (jump->second.exp)
	{
	  iprintf (fp, indet + 2, "expanded tokens:");

	  std::vector<expanded_token>::const_iterator tok;
	  const gcj::expansion *exp;
	  exp = unit->get_expansion (jump->second.exp);
	  for (tok = exp->tokens.begin ();
	       tok != exp->tokens.end ();
	       ++ tok)
	  fprintf (fp, " %d \"%s\"",
		   tok->id,
		   escape (tok->token.c_str (), '"').c_str ());
	  fprintf (fp, "\n");
	}
    }

  std::map<int, context>::const_iterator ctx;
  for (ctx = expansion_contexts.begin ();
       ctx != expansion_contexts.end ();
       ++ ctx)
    {
      iprintf (fp, indet, "expansion context %d:\n",
	       ctx->first);
      ctx->second.dump (fp, indet + 1, unit);
    }
}

static void
save_jump_from (FILE *fp, const jump_from& from)
{
  save_file_location (fp, from.loc);
  save_int32 (fp, from.len);
  save_int32 (fp, from.expanded_id);
}

static void
load_jump_from (FILE *fp, jump_from *from)
{
  load_file_location (fp, &from->loc);
  load_int32 (fp, &from->len);
  load_int32 (fp, &from->expanded_id);
}

static void
save_jump_to (FILE *fp, const jump_to& to)
{
  save_int32 (fp, to.include);
  save_int32 (fp, to.point);
  save_file_location (fp, to.loc);
  save_int32 (fp, to.expanded_id);
  save_int32 (fp, to.exp);
}

static void
load_jump_to (FILE *fp, jump_to *to)
{
  load_int32 (fp, &to->include);
  load_int32 (fp, &to->point);
  load_file_location (fp, &to->loc);
  load_int32 (fp, &to->expanded_id);
  load_int32 (fp, &to->exp);
}

void
context::save (FILE *fp) const
{
  save_int32 (fp, jumps.size ());
  std::map<jump_from, jump_to>::const_iterator jmp;
  for (jmp = jumps.begin (); jmp != jumps.end (); ++ jmp)
    {
      save_jump_from (fp, jmp->first);
      save_jump_to (fp, jmp->second);
    }

  save_int32 (fp, surrounding);

  save_int32 (fp, expansion_contexts.size ());
  std::map<int, context>::const_iterator ctx;
  for (ctx = expansion_contexts.begin ();
       ctx != expansion_contexts.end ();
       ++ ctx)
    {
      save_int32 (fp, ctx->first);
      ctx->second.save (fp);
    }
}

void
context::load (FILE *fp)
{
  int jmp_size;
  load_int32 (fp, &jmp_size);
  for (int i = 0; i < jmp_size; ++ i)
    {
      jump_from from;
      load_jump_from (fp, &from);

      jump_to to;
      load_jump_to (fp, &to);

      jumps.insert (std::make_pair (from, to));
    }

  load_int32 (fp, &surrounding);

  int ctx_size;
  load_int32 (fp, &ctx_size);
  for (int i = 0; i < ctx_size; ++ i)
    {
      int id;
      load_int32 (fp, &id);
      expansion_contexts.insert (std::make_pair (id, context ()));
      expansion_contexts.find (id)->second.load (fp);
    }
}

int
unit::file_id (const char *file)
{
  char *full = realpath (file, NULL);
  if (! full)
    return file_map.get (file);
  int id = file_map.get (full);
  free (full);
  return id;
}

static void
dump_srcs (FILE *fp, int ident,
	   const std::map<std::string, std::vector<jump_src> >& srcs)
{
  std::map<std::string, std::vector<jump_src> >::const_iterator it;
  for (it = srcs.begin (); it != srcs.end (); ++ it)
    {
      iprintf (fp, ident, "name: %s\n", it->first.c_str ());
      std::vector<jump_src>::const_iterator jt;
      for (jt = it->second.begin (); jt != it->second.end (); ++ jt)
	{
	  iprintf (fp, ident + 1, "include: %d, ", jt->include);
	  print_jump_src (fp, jt->from);
	  fprintf (fp, "\n");
	}
    }
}

static void
dump_tgts (FILE *fp, int ident,
	   const std::map<std::string, jump_tgt>& tgts)
{
  std::map<std::string, jump_tgt>::const_iterator it;
  for (it = tgts.begin (); it != tgts.end (); ++ it)
    {
      iprintf (fp, ident, "name: %s, ", it->first.c_str ());
      assert (it->second.to.include);
      print_jump_to (fp, it->second.to);
      fprintf (fp, ", weak: %d\n", it->second.weak);
    }
}

void
unit::dump (FILE *fp, int indet) const
{
  for (int i = 1; i <= include_map.size (); ++ i)
    {
      iprintf (fp, indet, "include %d:\n", i);
      std::vector<source_location>::const_iterator loc;
      for (loc = include_map.at (i).locs.begin ();
	   loc != include_map.at (i).locs.end ();
	   ++ loc)
	iprintf (fp, indet + 1, "from %s:%d,%d\n",
		 file_map.at (loc->fid).c_str (),
		 loc->loc.line, loc->loc.col);
    }

  std::map<int, context>::const_iterator ctx;
  for (ctx = contexts.begin (); ctx != contexts.end (); ++ ctx)
    {
      iprintf (fp, indet, "context %d:\n", ctx->first);
      ctx->second.dump (fp, indet + 1, this);
    }

  if (! pub_srcs.empty ())
    {
      iprintf (fp, indet, "jump_pub_srcs: \n");
      dump_srcs (fp, indet + 1, pub_srcs);
    }
  if (! pub_tgts.empty ())
    {
      iprintf (fp, indet, "jump_pub_tgts: \n");
      dump_tgts (fp, indet + 1, pub_tgts);
    }
}
static void
save_srcs (FILE *fp,
	   const std::map<std::string, std::vector<jump_src> >& srcs)
{
  save_int32 (fp, srcs.size ());
  std::map<std::string, std::vector<jump_src> >::const_iterator it;
  for (it = srcs.begin (); it != srcs.end (); ++ it)
    {
      save_string (fp, it->first);
      save_int32 (fp, it->second.size ());
      std::vector<jump_src>::const_iterator jt;
      for (jt = it->second.begin (); jt != it->second.end (); ++ jt)
	{
	  save_int32 (fp, jt->include);
	  save_jump_from (fp, jt->from);
	}
    }
}

static void
load_srcs (FILE *fp,
	   std::map<std::string, std::vector<jump_src> > *srcs)
{
  int map_size;
  load_int32 (fp, &map_size);
  for (int i = 0; i < map_size; ++ i)
    {
      std::string name;
      load_string (fp, &name);
      int vec_size;
      load_int32 (fp, &vec_size);
      std::vector<jump_src> vec;
      for (int j = 0; j < vec_size; ++ j)
	{
	  int include;
	  load_int32 (fp, &include);
	  jump_from from;
	  load_jump_from (fp, &from);
	  vec.push_back (jump_src (include, from));
	}
      srcs->insert (std::make_pair (name, vec));
    }
}

static void
save_tgts (FILE *fp, const std::map<std::string, jump_tgt>& tgts)
{
  save_int32 (fp, tgts.size ());
  std::map<std::string, jump_tgt>::const_iterator it;
  for (it = tgts.begin (); it != tgts.end (); ++ it)
    {
      save_string (fp, it->first);
      save_jump_to (fp, it->second.to);
      save_int32 (fp, it->second.weak);
    }
}

static void
load_tgts (FILE *fp, std::map<std::string, jump_tgt> *tgts)
{
  int map_size;
  load_int32 (fp, &map_size);
  for (int i = 0; i < map_size; ++ i)
    {
      std::string name;
      load_string (fp, &name);
      jump_to to;
      load_jump_to (fp, &to);
      int weak;
      load_int32 (fp, &weak);
      tgts->insert (std::make_pair (name, jump_tgt (to, (bool) weak)));
    }
}

static void
save_int32r (FILE *fp, const int &v)
{
  save_int32 (fp, v);
}

void
unit::save (FILE *fp) const
{
  save_string (fp, input);
  save_int32 (fp, input_id);

  save_int32 (fp, contexts.size ());
  std::map<int, context>::const_iterator ctx;
  for (ctx = contexts.begin (); ctx != contexts.end (); ++ ctx)
    {
      save_int32 (fp, ctx->first);
      ctx->second.save (fp);
    }

  save_int32 (fp, expansions.size ());
  std::map<int, expansion>::const_iterator exp;
  for (exp = expansions.begin (); exp != expansions.end (); ++ exp)
    {
      exp->second.map.save (fp, save_int32r);

      save_int32 (fp, exp->second.tokens.size ());
      std::vector<expanded_token>::const_iterator tok;
      for (tok = exp->second.tokens.begin ();
	   tok != exp->second.tokens.end ();
	   ++ tok)
	{
	  save_string (fp, tok->token);
	  save_int32 (fp, tok->id);
	}
    }

  file_map.save (fp, save_string);
  include_map.save (fp, save_source_stack);
  point_map.save (fp, save_expansion_point);

  save_srcs (fp, pub_srcs);
  save_tgts (fp, pub_tgts);
}

void
unit::load (FILE *fp)
{
  load_string (fp, &input);
  load_int32 (fp, &input_id);

  int ctx_size;
  load_int32 (fp, &ctx_size);
  for (int i = 0; i < ctx_size; ++ i)
    {
      int id;
      load_int32 (fp, &id);
      contexts.insert (std::make_pair (id, context ()));
      contexts.find (id)->second.load (fp);
    }

  int expansion_size;
  load_int32 (fp, &expansion_size);
  for (int i = 0; i < expansion_size; ++ i)
    {
      expansion *exp = get_expansion (get_expansion ());
      exp->map.load (fp, load_int32);

      int tok_size;
      load_int32 (fp, &tok_size);
      for (int j = 0; j < tok_size; ++ j)
	{
	  expanded_token tok;
	  load_string (fp, &tok.token);
	  load_int32 (fp, &tok.id);
	  exp->tokens.push_back (tok);
	}
    }

  file_map.load (fp, load_string);
  include_map.load (fp, load_source_stack);
  point_map.load (fp, load_expansion_point);

  load_srcs (fp, &pub_srcs);
  load_tgts (fp, &pub_tgts);
}

static std::string
index_path (const std::string& db)
{
  return joinpath (db.c_str (), "index", NULL);
}

static std::string
unit_path (const std::string& db, int id)
{
  return joinpath (db.c_str(), "units", tostr (id).c_str (), NULL);
}

void
set::next (const std::string& args, const std::string& input)
{
  if (cur_id)
    save_current_unit ();

  cur_id = unit_map.get (args);
  cur = unit (&log, input);
}

void
set::save_current_unit ()
{
  if (dump)
    {
      fprintf (stderr, "unit %s:\n", unit_map.at (cur_id).c_str ());
      cur.dump (stderr, 1);
    }
  FILE *fp = fopen (unit_path (db, cur_id).c_str (), "wb");
  assert (fp);
  cur.save (fp);
  fclose (fp);
}

void
set::save () const
{
  FILE *fp = fopen (index_path (db).c_str (), "wb");
  assert (fp);
  unit_map.save (fp, save_string);
  fclose (fp);
}

void
set::load ()
{
  FILE *fp = fopen (index_path (db).c_str (), "rb");
  if (! fp)
    return;
  unit_map.load (fp, load_string);
  fclose (fp);
}

void
set_usr::load ()
{
  FILE *fp = fopen (index_path (db).c_str (), "rb");
  assert (fp);
  unit_map.load (fp, load_string);
  fclose (fp);

  for (int i = 1; i <= unit_map.size (); ++ i)
    units.insert (std::make_pair (i, unit (NULL)));
}

const unit *
set_usr::get (int id)
{
  if (units.find (id) == units.end ())
    return NULL;

  if (loaded_units.find (id) == loaded_units.end ())
    {
      FILE *fp = fopen (unit_path (db, id).c_str (), "rb");
      assert (fp);
      units.find (id)->second.load (fp);
      fclose (fp);
    }

  return &units.find (id)->second;
}

void
add_jump_src (std::map<std::string, std::vector<jump_src> > *srcs,
	      const std::string& name, int include,
	      const jump_from& jump_from)
{
  if (srcs->find (name) == srcs->end ())
    srcs->insert (std::make_pair (name, std::vector<jump_src>()));
  srcs->find (name)->second.push_back (jump_src (include, jump_from));
}

void
add_jump_tgt (std::map<std::string, jump_tgt> *tgts,
	      const std::string& name,
	      const jump_to& jump_to, bool weak)
{
  if (tgts->find (name) == tgts->end ())
    tgts->insert (std::make_pair (name, jump_tgt (jump_to, weak)));
  // We could done more check here
  else if (tgts->find (name)->second.weak && ! weak)
    tgts->find (name)->second = jump_tgt (jump_to, weak);
}

}
