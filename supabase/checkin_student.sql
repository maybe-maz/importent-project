-- Supabase RPC: classify attendance by check-in delay
-- Rule:
-- 0-5 minutes   => present
-- >5-15 minutes => late
-- >15 minutes   => absent

create extension if not exists pgcrypto;

drop function if exists public.checkin_student(uuid, text, text, text);

create or replace function public.checkin_student(
  p_lecture_id uuid,
  p_student_id text,
  p_student_name text,
  p_password text
)
returns jsonb
language plpgsql
security definer
set search_path = public
as $$
declare
  v_lecture lectures%rowtype;
  v_now timestamptz := now();
  v_start_time time := time '00:00';
  v_diff_minutes numeric := 0;
  v_status text := 'present';
  v_note text := '-';
begin
  if p_lecture_id is null or p_student_id is null or p_student_name is null or p_password is null then
    raise exception 'Missing required fields.';
  end if;

  select *
  into v_lecture
  from public.lectures
  where id = p_lecture_id;

  if not found then
    raise exception 'Lecture not found.';
  end if;

  if coalesce(v_lecture.password, '') <> p_password then
    raise exception 'Wrong lecture password.';
  end if;

  -- start_time can be stored as text or time depending on schema revisions.
  if v_lecture.start_time is not null and v_lecture.start_time::text ~ '^\d{2}:\d{2}(:\d{2})?$' then
    v_start_time := v_lecture.start_time::text::time;
  end if;

  -- Compare check-in time to lecture start time on the same date.
  v_diff_minutes := extract(epoch from (v_now - date_trunc('day', v_now) - v_start_time)) / 60.0;

  if v_diff_minutes <= 5 then
    v_status := 'present';
    v_note := '-';
  elsif v_diff_minutes <= 15 then
    v_status := 'late';
    v_note := '-';
  else
    v_status := 'absent';
    v_note := 'Over 15 minutes late';
  end if;

  insert into public.attendance (
    lecture_id,
    student_id,
    student_name,
    checkin_time,
    status,
    note
  ) values (
    p_lecture_id,
    p_student_id,
    p_student_name,
    case when v_status = 'absent' then null else v_now end,
    v_status,
    v_note
  )
  on conflict (lecture_id, student_id)
  do update
  set
    student_name = excluded.student_name,
    checkin_time = excluded.checkin_time,
    status = excluded.status,
    note = excluded.note;

  return jsonb_build_object(
    'ok', true,
    'status', v_status,
    'note', v_note
  );
end;
$$;

grant usage on schema public to anon;
grant execute on function public.checkin_student(uuid, text, text, text) to anon;
