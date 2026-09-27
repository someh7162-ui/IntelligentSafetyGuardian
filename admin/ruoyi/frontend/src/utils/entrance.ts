const getEntrance = () => document.getElementById('loader-wrapper');

const nextPaint = () =>
  new Promise<void>(resolve => {
    requestAnimationFrame(() => requestAnimationFrame(() => resolve()));
  });

export const showEntrance = () => {
  const entrance = getEntrance();
  entrance?.classList.remove('is-loaded');
  return performance.now();
};

export const hideEntrance = async (shownAt = 0) => {
  await nextPaint();
  const remaining = 480 - (performance.now() - shownAt);
  if (shownAt && remaining > 0) {
    await new Promise(resolve => setTimeout(resolve, remaining));
  }
  getEntrance()?.classList.add('is-loaded');
};
