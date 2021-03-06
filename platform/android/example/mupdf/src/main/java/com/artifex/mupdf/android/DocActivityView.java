package com.artifex.mupdf.android;

import android.app.Activity;
import android.content.Context;
import android.graphics.PorterDuff;
import android.util.AttributeSet;
import android.util.Log;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.TabHost;
import android.widget.TextView;

import com.artifex.mupdf.fitz.Link;
import com.artifex.mupdf.fitz.Outline;
import com.artifex.mupdf.fitz.R;

public class DocActivityView extends FrameLayout implements TabHost.OnTabChangeListener, View.OnClickListener
{
	private DocView mDocView;
	private DocReflowView mDocReflowView;
	private DocListPagesView mDocPagesView;

	private boolean mShowUI = true;

	//  tab tags
	private String mTagHidden;
	private String mTagFile;
	private String mTagAnnotate;
	private String mTagPages;

	private ImageButton mReflowButton;
	private ImageButton mFirstPageButton;
	private ImageButton mLastPageButton;

	private ImageButton mSearchButton;
	private EditText mSearchText;
	private ImageButton mSearchNextButton;
	private ImageButton mSearchPreviousButton;

	public DocActivityView(Context context)
	{
		super(context);
	}

	public DocActivityView(Context context, AttributeSet attrs)
	{
		super(context, attrs);
	}

	public DocActivityView(Context context, AttributeSet attrs, int defStyle)
	{
		super(context, attrs, defStyle);
	}

	protected boolean usePagesView()
	{
		return true;
	}

	protected void setupTabs()
	{
		TabHost tabHost = (TabHost) findViewById(R.id.tabhost);
		tabHost.setup();

		//  get the tab tags.
		mTagHidden = getResources().getString(R.string.hidden_tab);
		mTagFile = getResources().getString(R.string.file_tab);
		mTagAnnotate = getResources().getString(R.string.annotate_tab);
		mTagPages = getResources().getString(R.string.pages_tab);

		//  first tab is and stays hidden.
		//  when the search tab is selected, we programmatically "select" this hidden tab
		//  which results in NO tabs appearing selected in this tab host.
		setupTab(tabHost, mTagHidden, R.id.hiddenTab, R.layout.tab);
		tabHost.getTabWidget().getChildTabViewAt(0).setVisibility(View.GONE);

		//  these tabs are shown.
		setupTab(tabHost, mTagFile, R.id.fileTab, R.layout.tab_left);
		setupTab(tabHost, mTagAnnotate, R.id.annotateTab, R.layout.tab);
		setupTab(tabHost, mTagPages, R.id.pagesTab, R.layout.tab_right);

		//  start by showing the edit tab
		tabHost.setCurrentTabByTag(mTagFile);

		tabHost.setOnTabChangedListener(this);
	}

	protected void setupTab(TabHost tabHost, String text, int viewId, int tabId)
	{
		View tabview = LayoutInflater.from(tabHost.getContext()).inflate(tabId, null);
		TextView tv = (TextView) tabview.findViewById(R.id.tabText);
		tv.setText(text);

		TabHost.TabSpec tab = tabHost.newTabSpec(text);
		tab.setIndicator(tabview);
		tab.setContent(viewId);
		tabHost.addTab(tab);
	}

	@Override
	public void onTabChanged(String tabId)
	{
		//  hide the search tab
		findViewById(R.id.searchTab).setVisibility(View.GONE);

		//  show search is not selected
		showSearchSelected(false);

		//  show/hide the pages view
		handlePagesTab(tabId);

		hideKeyboard();
	}

	private void showSearchSelected(boolean selected)
	{
		//  set the view
		mSearchButton.setSelected(selected);

		//  colorize
		if (selected)
			mSearchButton.setColorFilter(0xff000000, PorterDuff.Mode.SRC_IN);
		else
			mSearchButton.setColorFilter(0xffffffff, PorterDuff.Mode.SRC_IN);
	}

	protected void handlePagesTab(String tabId)
	{
		if (tabId.equals(mTagPages))
			showPages();
		else
		{
			hideReflow();
			hidePages();
		}
	}

	protected void showPages()
	{
		LinearLayout pages = (LinearLayout) findViewById(R.id.pages_container);
		if (null == pages)
			return;

		if (pages.getVisibility() == View.VISIBLE)
			return;

		pages.setVisibility(View.VISIBLE);
		ViewTreeObserver observer = mDocView.getViewTreeObserver();
		observer.addOnGlobalLayoutListener(new ViewTreeObserver.OnGlobalLayoutListener()
		{
			@Override
			public void onGlobalLayout()
			{
				mDocView.getViewTreeObserver().removeOnGlobalLayoutListener(this);
				mDocView.onShowPages();
			}
		});
	}

	protected void hidePages()
	{
		LinearLayout pages = (LinearLayout) findViewById(R.id.pages_container);
		if (null == pages)
			return;

		if (pages.getVisibility() == View.GONE)
			return;

		pages.setVisibility(View.GONE);
		ViewTreeObserver observer = mDocView.getViewTreeObserver();
		observer.addOnGlobalLayoutListener(new ViewTreeObserver.OnGlobalLayoutListener()
		{
			@Override
			public void onGlobalLayout()
			{
				mDocView.getViewTreeObserver().removeOnGlobalLayoutListener(this);
				mDocView.onHidePages();
			}
		});
	}

	public boolean showKeyboard()
	{
		//  show keyboard
		InputMethodManager im = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
		im.toggleSoftInput(InputMethodManager.SHOW_FORCED, InputMethodManager.HIDE_IMPLICIT_ONLY);

		return true;
	}

	public void hideKeyboard()
	{
		//  hide the keyboard
		InputMethodManager im = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
		im.hideSoftInputFromWindow(mDocView.getWindowToken(), 0);
	}

	private boolean started = false;
	public void start(final String path)
	{
		started = false;

		((Activity)getContext()).getWindow().addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN);
		((Activity)getContext()).getWindow().addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);

		//  inflate the UI
		final LayoutInflater inflater = (LayoutInflater) getContext().getSystemService(Context.LAYOUT_INFLATER_SERVICE);
		final LinearLayout view = (LinearLayout) inflater.inflate(R.layout.doc_view, null);

		final ViewTreeObserver vto = getViewTreeObserver();
		vto.addOnGlobalLayoutListener(new ViewTreeObserver.OnGlobalLayoutListener()
		{
			@Override
			public void onGlobalLayout()
			{
				if (!started)
				{
					findViewById(R.id.tabhost).setVisibility(mShowUI?View.VISIBLE:View.GONE);
					findViewById(R.id.footer).setVisibility(mShowUI?View.VISIBLE:View.GONE);
					afterFirstLayoutComplete(path);

					started = true;
				}
			}
		});

		addView(view);
	}

	public void afterFirstLayoutComplete(final String path)
	{
		//  main view
		mDocView = (DocView) findViewById(R.id.doc_view_inner);
		mDocReflowView = (DocReflowView) findViewById(R.id.doc_reflow_view);

		//  page list
		if (usePagesView())
		{
			mDocPagesView = new DocListPagesView(getContext());
			mDocPagesView.setSelectionListener(new DocListPagesView.SelectionListener()
			{
				@Override
				public void onPageSelected(int pageNumber)
				{
					goToPage(pageNumber);
				}
			});

			LinearLayout layout2 = (LinearLayout) findViewById(R.id.pages_container);
			layout2.addView(mDocPagesView);
		}

		//  tabs
		setupTabs();

		//  selection handles
		View v = findViewById(R.id.doc_wrapper);
		RelativeLayout layout = (RelativeLayout) v;
		mDocView.setupHandles(layout);

		//  listen for layout changes on the main doc view, and
		//  copy the "most visible" value to the page list.
		ViewTreeObserver observer2 = mDocView.getViewTreeObserver();
		observer2.addOnGlobalLayoutListener(new ViewTreeObserver.OnGlobalLayoutListener()
		{
			@Override
			public void onGlobalLayout()
			{
				if (usePagesView())
				{
					if (mDocView.getVisibility() == View.VISIBLE)
					{
						int mvp = mDocView.getMostVisiblePage();
						mDocPagesView.setMostVisiblePage(mvp);
					}
				}
			}
		});

		//  connect buttons to functions

		mReflowButton = (ImageButton)findViewById(R.id.reflow_button);
		mReflowButton.setOnClickListener(this);

		mFirstPageButton = (ImageButton)findViewById(R.id.first_page_button);
		mFirstPageButton.setOnClickListener(this);

		mLastPageButton = (ImageButton)findViewById(R.id.last_page_button);
		mLastPageButton.setOnClickListener(this);

		mSearchButton = (ImageButton)findViewById(R.id.search_button);
		mSearchButton.setOnClickListener(this);
		showSearchSelected(false);
		mSearchText = (EditText) findViewById(R.id.search_text_input);
		mSearchText.setOnClickListener(this);

		//  this listener will
		mSearchText.setOnEditorActionListener(new TextView.OnEditorActionListener()
		{
			@Override
			public boolean onEditorAction(TextView v, int actionId, KeyEvent event)
			{
				if (actionId == EditorInfo.IME_ACTION_NEXT)
				{
					onSearchNextButton();
					return true;
				}
				return false;
			}
		});

		mSearchNextButton = (ImageButton)findViewById(R.id.search_next_button);
		mSearchNextButton.setOnClickListener(this);

		mSearchPreviousButton = (ImageButton)findViewById(R.id.search_previous_button);
		mSearchPreviousButton.setOnClickListener(this);

		//  start the views
		mDocView.start(path);
		if (usePagesView())
		{
			mDocPagesView.clone(mDocView);
		}
	}

	public void showUI(boolean show)
	{
		mShowUI = show;
	}

	public void stop()
	{
		mDocView.finish();
		if (usePagesView())
		{
			mDocPagesView.finish();
		}
	}

	private void onOutline(final Outline[] outline, int level)
	{
		if (outline == null)
			return;

		for (Outline entry : outline)
		{
			int numberOfSpaces = (level) * 4;
			String spaces = "";
			if (numberOfSpaces > 0)
				spaces = String.format("%" + numberOfSpaces + "s", " ");
			Log.i("example", String.format("%d %s %s %s", entry.page + 1, spaces, entry.title, entry.uri));
			if (entry.down != null)
			{
				//  branch
				onOutline(entry.down, level + 1);
			}
		}
	}

	private void onLinks()
	{
		int numPages = mDocView.getPageCount();
		for (int pageNum = 0; pageNum < numPages; pageNum++)
		{
			DocPageView cv = (DocPageView) mDocView.getOrCreateChild(pageNum);

			Link links[] = cv.getPage().getLinks();
			if (links != null)
			{

				for (int i = 0; i < links.length; i++)
				{
					Link link = links[i];

					Log.i("example", String.format("links for page %d:", pageNum));
					Log.i("example", String.format("     link %d:", i));
					Log.i("example", String.format("          page = %d", link.page));
					Log.i("example", String.format("          uri = %s", link.uri));
					Log.i("example", String.format("          bounds = %f %f %f %f ",
							link.bounds.x0, link.bounds.y0, link.bounds.x1, link.bounds.y1));
				}
			}
			else
			{
				Log.i("example", String.format("no links for page %d", pageNum));
			}

		}
	}

	@Override
	public void onClick(View v)
	{
		if (v == mReflowButton)
			onReflowButton();
		if (v == mFirstPageButton)
			onFirstPageButton();
		if (v == mLastPageButton)
			onLastPageButton();
		if (v == mSearchButton)
			onShowSearch();
		if (v == mSearchText)
			onEditSearchText();
		if (v == mSearchNextButton)
			onSearchNextButton();
		if (v == mSearchPreviousButton)
			onSearchPreviousButton();
	}

	public void onSearchNextButton()
	{
		hideKeyboard();
		mDocView.onSearchNext(mSearchText.getText().toString());
	}

	public void onSearchPreviousButton()
	{
		hideKeyboard();
		mDocView.onSearchPrevious(mSearchText.getText().toString());
	}

	public void onEditSearchText()
	{
		mSearchText.requestFocus();
		showKeyboard();
	}

	public void onShowSearch()
	{
		//  "deselect" all the visible tabs by selecting the hidden (first) one
		TabHost tabHost = (TabHost)findViewById(R.id.tabhost);
		tabHost.setCurrentTabByTag("HIDDEN");

		//  show search as selected
		showSearchSelected(true);

		//  hide all the other tabs
		hideAllTabs();

		//  show the search tab
		findViewById(R.id.searchTab).setVisibility(View.VISIBLE);
		mSearchText.getText().clear();
	}

	protected void hideAllTabs()
	{
		//  hide all the other tabs
		findViewById(R.id.fileTab).setVisibility(View.GONE);
		findViewById(R.id.annotateTab).setVisibility(View.GONE);
		findViewById(R.id.pagesTab).setVisibility(View.GONE);
	}

	private void onFirstPageButton()
	{
		goToPage(0);
	}

	private void onLastPageButton()
	{
		int npages = mDocView.getPageCount();
		goToPage(npages-1);
	}

	private void goToPage(int pageNumber)
	{
		mDocView.scrollToPage(pageNumber);

		if (mDocReflowView.getVisibility() == View.VISIBLE)
		{
			setReflowText(pageNumber);
			mDocPagesView.setMostVisiblePage(pageNumber);
		}
	}

	private void onReflowButton()
	{
		if (mDocView.getVisibility() == View.VISIBLE)
		{
			//  set initial text into reflow view
			setReflowText(mDocPagesView.getMostVisiblePage());

			//  show reflow
			showReflow();
		}
		else
		{
			//  hide reflow
			hideReflow();
		}
	}

	private void showReflow()
	{
		mDocView.setVisibility(View.GONE);
		mDocReflowView.setVisibility(View.VISIBLE);
	}

	private void hideReflow()
	{
		mDocReflowView.setVisibility(View.GONE);
		mDocView.setVisibility(View.VISIBLE);
	}

	private void setReflowText(int pageNumber)
	{
		DocPageView dpv = (DocPageView)mDocView.getAdapter().getView(pageNumber, null, null);
		byte bytes[] = dpv.getPage().textAsHtml();
		mDocReflowView.setHTML(bytes);
	}
}
